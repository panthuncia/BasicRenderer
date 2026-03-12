#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/loadingUtils.hlsli"
#include "Common/defines.h"
#include "include/meshletPayload.hlsli"
#include "Include/meshletCommon.hlsli"
#include "Include/clodStructs.hlsli"
#include "Include/clodPageAccess.hlsli"

#define CLOD_COMPRESSED_POSITIONS 1u
#define CLOD_COMPRESSED_MESHLET_VERTEX_INDICES 2u
#define CLOD_COMPRESSED_NORMALS 4u

uint ReadPackedBits32(StructuredBuffer<uint> words, uint startBit, uint bitCount)
{
    if (bitCount == 0u)
    {
        return 0u;
    }

    uint wordIndex = startBit >> 5;
    uint bitOffset = startBit & 31u;
    uint packed = words[wordIndex] >> bitOffset;
    if (bitOffset + bitCount > 32u)
    {
        packed |= words[wordIndex + 1u] << (32u - bitOffset);
    }

    uint mask = (bitCount >= 32u) ? 0xffffffffu : ((1u << bitCount) - 1u);
    return packed & mask;
}

// ByteAddressBuffer variant for page-pool slab reads.
uint ReadPackedBits32_BA(ByteAddressBuffer buf, uint startBit, uint bitCount)
{
    if (bitCount == 0u)
    {
        return 0u;
    }

    uint wordIndex = startBit >> 5;
    uint bitOffset = startBit & 31u;
    uint packed = buf.Load(wordIndex * 4u) >> bitOffset;
    if (bitOffset + bitCount > 32u)
    {
        packed |= buf.Load((wordIndex + 1u) * 4u) << (32u - bitOffset);
    }

    uint mask = (bitCount >= 32u) ? 0xffffffffu : ((1u << bitCount) - 1u);
    return packed & mask;
}

float3 DecodeCompressedPosition(
    uint groupLocalVertexIndex,
    uint compressedPositionWordsBase,
    uint bitsX,
    uint bitsY,
    uint bitsZ,
    uint quantExp,
    int3 minQ,
    uint pagePoolSlabDescriptorIndex)
{
    uint bitsPerVertex = bitsX + bitsY + bitsZ;
    uint bitCursor = compressedPositionWordsBase * 32u + groupLocalVertexIndex * bitsPerVertex;

    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint px = ReadPackedBits32_BA(slab, bitCursor, bitsX);
    bitCursor += bitsX;
    uint py = ReadPackedBits32_BA(slab, bitCursor, bitsY);
    bitCursor += bitsY;
    uint pz = ReadPackedBits32_BA(slab, bitCursor, bitsZ);

    int3 q = int3(px, py, pz) + minQ;
    float invScale = 1.0f / float(1u << quantExp);
    return float3(q) * invScale;
}

float2 UnpackSnorm16x2(uint packed)
{
    int signedPacked = asint(packed);
    int x = (signedPacked << 16) >> 16;
    int y = signedPacked >> 16;
    float sx = max(-1.0f, (float)x / 32767.0f);
    float sy = max(-1.0f, (float)y / 32767.0f);
    return float2(sx, sy);
}

float3 OctDecodeNormal(float2 e)
{
    float3 v = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
    if (v.z < 0.0f)
    {
        float2 folded = (1.0f - abs(v.yx)) * float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
        v.x = folded.x;
        v.y = folded.y;
    }
    return normalize(v);
}

float3 DecodeCompressedNormal(uint groupLocalVertexIndex, uint compressedNormalWordsBase, uint pagePoolSlabDescriptorIndex)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint packed = slab.Load((compressedNormalWordsBase + groupLocalVertexIndex) * 4u);
    return OctDecodeNormal(UnpackSnorm16x2(packed));
}

VisBufferPSInput BuildVisBufferVertexAttributesForView(
    Vertex vertex,
    uint3 vGroupID,
    PerObjectBuffer objectBuffer,
    uint viewID,
    uint clusterIndex,
    uint materialDataIndex,
    ClodViewRasterInfo rasterInfo)
{
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera viewCamera = cameras[viewID];

    float4 pos = float4(vertex.position.xyz, 1.0f);
    float4 worldPosition = mul(pos, objectBuffer.model);
    float4 viewPosition = mul(worldPosition, viewCamera.view);

    VisBufferPSInput result;
    result.position = mul(viewPosition, viewCamera.projection);
    result.position.x = result.position.x * rasterInfo.viewportScaleX + result.position.w * (rasterInfo.viewportScaleX - 1.0f);
    result.position.y = result.position.y * rasterInfo.viewportScaleY + result.position.w * (1.0f - rasterInfo.viewportScaleY);
    result.visibleClusterIndex = clusterIndex;
    result.linearDepth = -viewPosition.z;
    result.viewID = viewID;
#if defined(PSO_ALPHA_TEST)
    result.texcoord = vertex.texcoord;
    result.materialDataIndex = materialDataIndex;
#endif

    return result;
}

PSInput GetVertexAttributes(uint blockByteOffset, uint prevBlockByteOffset, uint index, uint flags, uint vertexSize, uint3 vGroupID, PerObjectBuffer objectBuffer) {
    uint byteOffset = blockByteOffset + index * vertexSize;
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    Vertex vertex = LoadVertex(byteOffset, vertexBuffer, flags);
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    float4 pos = float4(vertex.position.xyz, 1.0f);
    float4 prevPos;
    if (flags & VERTEX_SKINNED)
    {
        uint prevByteOffset = prevBlockByteOffset + index * vertexSize;
        prevPos = float4(LoadFloat3(prevByteOffset, vertexBuffer), 1.0);
    }
    else
    {
        prevPos = float4(vertex.position.xyz, 1.0f);
    }

    float4 worldPosition = mul(pos, objectBuffer.model);
    PSInput result;

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];

    if (flags & VERTEX_TEXCOORDS) {
        result.texcoord = vertex.texcoord;
    }
    
#if defined(PSO_SHADOW)
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Light::InfoBuffer)];
    LightInfo light = lights[currentLightID];
    matrix lightMatrix;
    matrix viewMatrix;
    switch(light.type) {
        case 0: { // Point light
            StructuredBuffer<unsigned int> pointLightCubemapIndicesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Light::PointLightCubemapBuffer)];
            uint lightCameraIndex = pointLightCubemapIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            viewMatrix = lightCamera.view;
            break;
        }
        case 1: { // Spot light
            StructuredBuffer<unsigned int> spotLightMapIndicesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Light::SpotLightMatrixBuffer)];
            uint lightCameraIndex = spotLightMapIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            viewMatrix = lightCamera.view;
            break;
        }
        case 2: { // Directional light
            StructuredBuffer<unsigned int> directionalLightCascadeIndicesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Light::DirectionalLightCascadeBuffer)];
            uint lightCameraIndex = directionalLightCascadeIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            viewMatrix = lightCamera.view;
            break;
        }
    }
    result.position = mul(worldPosition, lightMatrix);
    result.positionViewSpace = mul(worldPosition, viewMatrix);
    return result;
#endif // SHADOW
    
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    result.positionWorldSpace = worldPosition;
    float4 viewPosition = mul(worldPosition, mainCamera.view);
    result.positionViewSpace = viewPosition;
    result.position = mul(viewPosition, mainCamera.projection);
    result.clipPosition = mul(viewPosition, mainCamera.unjitteredProjection);
    
    float4 prevPosition = mul(prevPos, objectBuffer.model);
    prevPosition = mul(prevPosition, mainCamera.prevView);
    result.prevClipPosition = mul(prevPosition, mainCamera.unjitteredProjection);
    
    if (flags & VERTEX_SKINNED) {
        result.normalWorldSpace = normalize(vertex.normal);
    }
    else {
        StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::NormalMatrixBuffer)];
        float3x3 normalMatrix = (float3x3) normalMatrixBuffer[objectBuffer.normalMatrixBufferIndex];
        result.normalWorldSpace = normalize(mul(vertex.normal, normalMatrix));
    }
    
    if (flags & VERTEX_COLORS) {
        result.color = vertex.color;
    };
    
    result.meshletIndex = vGroupID.x;
    
    result.normalModelSpace = normalize(vertex.normal);
    
    return result;
}

VisBufferPSInput GetVisBufferVertexAttributesForView(
    uint blockByteOffset,
    uint index,
    uint flags,
    uint vertexSize,
    uint3 vGroupID,
    PerObjectBuffer objectBuffer,
    uint viewID,
    uint clusterIndex,
    uint materialDataIndex,
    ClodViewRasterInfo rasterInfo)
{
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    uint byteOffset = blockByteOffset + index * vertexSize;
    Vertex vertex = LoadVertex(byteOffset, vertexBuffer, flags);
    return BuildVisBufferVertexAttributesForView(
        vertex,
        vGroupID,
        objectBuffer,
        viewID,
        clusterIndex,
        materialDataIndex,
        rasterInfo);
}

void WriteTriangles(uint uGroupThreadID, MeshletSetup setup, out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    bool reverseWinding = (setup.objectBuffer.objectFlags & OBJECT_FLAG_REVERSE_WINDING) != 0;
    for (uint t = uGroupThreadID; t < setup.triCount; t += MS_THREAD_GROUP_SIZE)
    {
        uint3 tri = DecodeTriangle(t, setup);
        outputTriangles[t] = reverseWinding ? tri.xzy : tri;
    }
}

// Emit for GBuffer path
void EmitMeshletGBuffer(uint uGroupThreadID, MeshletSetup setup, out vertices PSInput outputVertices[MS_MESHLET_SIZE], out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        outputVertices[i] = GetVertexAttributes(
            setup.postSkinningBufferOffset,
            setup.prevPostSkinningBufferOffset,
            setup.vertOffset + i,
            setup.meshBuffer.vertexFlags,
            setup.meshBuffer.vertexByteSize,
            setup.meshletIndex,
            setup.objectBuffer);
    }

    WriteTriangles(uGroupThreadID, setup, outputTriangles);
}

// Emit for Visibility Buffer path
void EmitMeshletVisBufferForView(
    uint uGroupThreadID,
    MeshletSetup setup,
    uint viewID,
    uint clusterIndex,
    ClodViewRasterInfo rasterInfo,
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        outputVertices[i] = GetVisBufferVertexAttributesForView(
            setup.postSkinningBufferOffset,
            setup.vertOffset + i,
            setup.meshBuffer.vertexFlags,
            setup.meshBuffer.vertexByteSize,
            setup.meshletIndex,
            setup.objectBuffer,
            viewID,
            clusterIndex,
            setup.meshBuffer.materialDataIndex,
            rasterInfo
        );
    }

    WriteTriangles(uGroupThreadID, setup, outputTriangles);
}

VisBufferPSInput GetVisBufferVertexAttributesForViewIndexed(
    uint meshletVerticesBaseOffset,
    uint compressedPositionWordsBase,
    uint compressedPositionBitsX,
    uint compressedPositionBitsY,
    uint compressedPositionBitsZ,
    uint compressedPositionQuantExp,
    int3 compressedPositionMinQ,
    uint compressedNormalWordsBase,
    uint compressedMeshletVertexWordsBase,
    uint compressedMeshletVertexBits,
    uint compressedFlags,
    uint meshletVertexIndex,
    uint blockByteOffset,
    uint flags,
    uint vertexSize,
    uint3 vGroupID,
    PerObjectBuffer objectBuffer,
    uint viewID,
    uint clusterIndex,
    uint materialDataIndex,
    ClodViewRasterInfo rasterInfo,
    uint pagePoolSlabDescriptorIndex)
{
    // CLod path: all buffers live in the slab.
    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    ByteAddressBuffer vertexBuffer = slab;

    uint groupLocalVertexIndex = 0;
    if ((compressedFlags & 2u) != 0u && compressedMeshletVertexBits > 0u)
    {
        uint startBit = compressedMeshletVertexWordsBase * 32u + meshletVertexIndex * compressedMeshletVertexBits;
        groupLocalVertexIndex = ReadPackedBits32_BA(slab, startBit, compressedMeshletVertexBits);
    }
    else
    {
        groupLocalVertexIndex = slab.Load((meshletVerticesBaseOffset + meshletVertexIndex) * 4u);
    }

    uint byteOffset = blockByteOffset + groupLocalVertexIndex * vertexSize;
    Vertex vertex = LoadVertex(byteOffset, vertexBuffer, flags);
    if ((flags & VERTEX_SKINNED) == 0u && (compressedFlags & CLOD_COMPRESSED_POSITIONS) != 0u)
    {
        vertex.position = DecodeCompressedPosition(
            groupLocalVertexIndex,
            compressedPositionWordsBase,
            compressedPositionBitsX,
            compressedPositionBitsY,
            compressedPositionBitsZ,
            compressedPositionQuantExp,
            compressedPositionMinQ,
            pagePoolSlabDescriptorIndex);
    }
    if ((flags & VERTEX_SKINNED) == 0u && (compressedFlags & CLOD_COMPRESSED_NORMALS) != 0u)
    {
        vertex.normal = DecodeCompressedNormal(groupLocalVertexIndex, compressedNormalWordsBase, pagePoolSlabDescriptorIndex);
    }

    return BuildVisBufferVertexAttributesForView(
        vertex,
        vGroupID,
        objectBuffer,
        viewID,
        clusterIndex,
        materialDataIndex,
        rasterInfo);
}

void EmitMeshletVisBufferForViewIndexed(
    uint uGroupThreadID,
    MeshletSetup setup,
    uint viewID,
    uint clusterIndex,
    ClodViewRasterInfo rasterInfo,
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        uint meshletVertexIndex = setup.vertOffset + i;
        outputVertices[i] = GetVisBufferVertexAttributesForViewIndexed(
            setup.groupMeshletVerticesBase,
            setup.compressedPositionWordsBase,
            setup.compressedPositionBitsX,
            setup.compressedPositionBitsY,
            setup.compressedPositionBitsZ,
            setup.compressedPositionQuantExp,
            setup.compressedPositionMinQ,
            setup.compressedNormalWordsBase,
            setup.compressedMeshletVertexWordsBase,
            setup.compressedMeshletVertexBits,
            setup.compressedFlags,
            meshletVertexIndex,
            setup.postSkinningBufferOffset,
            setup.meshBuffer.vertexFlags,
            setup.meshBuffer.vertexByteSize,
            setup.meshletIndex,
            setup.objectBuffer,
            viewID,
            clusterIndex,
            setup.meshBuffer.materialDataIndex,
            rasterInfo,
            setup.pagePoolSlabDescriptorIndex
        );
    }

    WriteTriangles(uGroupThreadID, setup, outputTriangles);
}

struct VisibilityPerPrimitive
{
    uint triangleIndex : SV_PrimitiveID;
    //uint viewID : TEXCOORD0;
};

void EmitPrimitiveIDs(uint uGroupThreadID, MeshletSetup setup, out primitives VisibilityPerPrimitive primitiveInfo[MS_MESHLET_SIZE])
{
    for (uint t = uGroupThreadID; t < setup.triCount; t += MS_THREAD_GROUP_SIZE)
    {
        primitiveInfo[t].triangleIndex = t;
		//primitiveInfo[t].viewID = setup.viewID;
    }
}

// Old AS+MS entries
[outputtopology("triangle")]
[numthreads(MS_THREAD_GROUP_SIZE, 1, 1)]
void MSMain(
    const uint uGroupThreadID : SV_GroupThreadID,
    const uint vGroupID : SV_GroupID,
    in payload Payload payload,
    out vertices PSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    
    uint meshletIndex = payload.MeshletIndices[vGroupID];
    MeshletSetup setup;
    bool draw = InitializeMeshlet(meshletIndex, setup);
    SetMeshOutputCounts(setup.vertCount, setup.triCount);
    if (!draw)
    {
        return;
    }
    EmitMeshletGBuffer(uGroupThreadID, setup, outputVertices, outputTriangles);
}

[outputtopology("triangle")]
[numthreads(MS_THREAD_GROUP_SIZE, 1, 1)]
void VisibilityBufferMSMain(
    const uint uGroupThreadID : SV_GroupThreadID,
    const uint vGroupID : SV_GroupID,
    in payload Payload payload,
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE],
    out primitives VisibilityPerPrimitive primitiveInfo[MS_MESHLET_SIZE])
{
    uint meshletIndex = payload.MeshletIndices[vGroupID];
    MeshletSetup setup;
    bool draw = InitializeMeshlet(meshletIndex, setup);
    SetMeshOutputCounts(setup.vertCount, setup.triCount);
    if (!draw)
    {
        return;
    }
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[setup.viewID];
    EmitMeshletVisBufferForView(uGroupThreadID, setup, setup.viewID, 0, viewRasterInfo, outputVertices, outputTriangles);
    EmitPrimitiveIDs(uGroupThreadID, setup, primitiveInfo);
}

#include "PerPassRootConstants/clodRootConstants.h"

bool InitializeMeshletFromCompactedCluster(VisibleCluster cluster, out MeshletSetup setup, in uint bucketMeshletIndex, in uint bucketCount)
{
	// if (bucketMeshletIndex >= bucketCount)
    // {
    //     setup.vertCount = 0;
    //     setup.triCount = 0;
    //     return false;
    // }	

    StructuredBuffer<PerMeshInstanceBuffer> meshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];

    setup.meshletIndex = cluster.localMeshletIndex;
    setup.meshInstanceBuffer = meshInstanceBuffer[cluster.instanceID];
    setup.viewID = cluster.viewID;

    // TODO: Fetch only necessary data for visibility buffer
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    setup.meshBuffer = perMeshBuffer[setup.meshInstanceBuffer.perMeshBufferIndex];
    setup.objectBuffer = perObjectBuffer[setup.meshInstanceBuffer.perObjectBufferIndex];

    // Use pre-resolved page address from VisibleCluster
    const uint pageSlabDesc = cluster.pageSlabDescriptorIndex;
    const uint pageSlabOff  = cluster.pageSlabByteOffset;
    if (pageSlabDesc == 0)
    {
        return false;
    }

    CLodPageHeader hdr = LoadPageHeader(pageSlabDesc, pageSlabOff);

    setup.groupVertexBase = 0;
    setup.groupVertexCount = hdr.vertexCount;
    setup.groupMeshletVertexCount = hdr.meshletVertexCount;
    setup.compressedPositionWordCount = hdr.compressedPositionWordCount;
    setup.compressedPositionBitsX = hdr.compressedPositionBitsX;
    setup.compressedPositionBitsY = hdr.compressedPositionBitsY;
    setup.compressedPositionBitsZ = hdr.compressedPositionBitsZ;
    setup.compressedPositionQuantExp = hdr.compressedPositionQuantExp;
    setup.compressedPositionMinQ = int3(
        hdr.compressedPositionMinQx,
        hdr.compressedPositionMinQy,
        hdr.compressedPositionMinQz);
    setup.compressedNormalWordCount = hdr.compressedNormalWordCount;
    setup.compressedMeshletVertexWordCount = hdr.compressedMeshletVertexWordCount;
    setup.compressedMeshletVertexBits = hdr.compressedMeshletVertexBits;
    setup.compressedFlags = hdr.compressedFlags;

    // Page-pool addressing from pre-resolved page location
    setup.pagePoolSlabDescriptorIndex = pageSlabDesc;
    {
        uint base = pageSlabOff;
        setup.groupVertexChunkByteOffset       = base + hdr.vertexOffset;
        setup.groupMeshletVerticesBase         = (base + hdr.meshletVertexOffset) / 4u;
        setup.groupMeshletTrianglesByteOffset  = base + hdr.triangleOffset;
        setup.compressedPositionWordsBase      = (base + hdr.compPosOffset) / 4u;
        setup.compressedNormalWordsBase        = (base + hdr.compNormOffset) / 4u;
        setup.compressedMeshletVertexWordsBase = (base + hdr.compMeshletVertOffset) / 4u;
    }

    // meshletIndex is now page-local
    if (setup.meshletIndex >= hdr.meshletCount)
    {
        return false;
    }

    // Load meshlet from the page-pool slab
    {
        uint meshletAddr = pageSlabOff + hdr.meshletStructOffset + setup.meshletIndex * 16u;
        setup.meshlet = LoadMeshletFromSlab(pageSlabDesc, meshletAddr);
    }

    setup.vertCount = setup.meshlet.VertCount;
    setup.triCount = setup.meshlet.TriCount;
    setup.vertOffset = setup.meshlet.VertOffset;

    if (setup.vertOffset + setup.vertCount > setup.groupMeshletVertexCount)
    {
        return false;
    }
    if (setup.meshlet.TriOffset + setup.meshlet.TriCount * 3u > hdr.meshletTrianglesByteCount)
    {
        return false;
    }

    // Vertex data lives in the slab page.
    {
        setup.postSkinningBufferOffset     = pageSlabOff + hdr.vertexOffset;
        setup.prevPostSkinningBufferOffset = setup.postSkinningBufferOffset;
    }

    return true;
}

// New pure-MS entry for Cluster LOD rendering
[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MS_THREAD_GROUP_SIZE, 1, 1)]
void ClusterLODBucketMSMain(
    const uint uGroupThreadID : SV_GroupThreadID,
    const uint3 vGroupID : SV_GroupID,
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE],
    out primitives VisibilityPerPrimitive primitiveInfo[MS_MESHLET_SIZE])
{
    // From command signature
    uint baseOffset = IndirectCommandSignatureRootConstant0;
    uint dispatchX = IndirectCommandSignatureRootConstant1;
    uint bucketIndex = IndirectCommandSignatureRootConstant2;

    uint linearizedID = vGroupID.x + vGroupID.y * dispatchX;

    StructuredBuffer<uint> histogram = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    uint count = histogram[bucketIndex];

    bool draw = linearizedID < count;
    VisibleCluster cluster = (VisibleCluster)0;
    MeshletSetup setup;
    uint visibleClusterIndex = baseOffset + linearizedID;

    if (draw) {   
        StructuredBuffer<VisibleCluster> compactedClusters = ResourceDescriptorHeap[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
        cluster = compactedClusters[visibleClusterIndex];
        draw = InitializeMeshletFromCompactedCluster(cluster, setup, linearizedID, count);
    } else {
        setup.vertCount = 0;
        setup.triCount = 0;
    }
    SetMeshOutputCounts(setup.vertCount, setup.triCount); // DXC won't accept SetMeshOutputCounts in non-uniform flow-control
    if (draw)
    {
        ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[setup.viewID];
        EmitMeshletVisBufferForViewIndexed(uGroupThreadID, setup, setup.viewID, visibleClusterIndex, viewRasterInfo, outputVertices, outputTriangles);
        EmitPrimitiveIDs(uGroupThreadID, setup, primitiveInfo);
    }
}