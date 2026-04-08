#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/skinningCommon.hlsli"
#include "include/loadingUtils.hlsli"
#include "Common/defines.h"
#include "include/meshletPayload.hlsli"
#include "include/meshletCommon.hlsli"
#include "include/clodStructs.hlsli"
#include "include/clodPageAccess.hlsli"
#include "include/clodVirtualShadowClipmap.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"

#define CLOD_COMPRESSED_POSITIONS 1u
#define CLOD_COMPRESSED_NORMALS 4u

#ifndef CLOD_RASTER_OUTPUT_VIRTUAL_SHADOW
#define CLOD_RASTER_OUTPUT_VIRTUAL_SHADOW 0
#endif

#if CLOD_RASTER_OUTPUT_VIRTUAL_SHADOW
static const uint kClodInvalidTriangleOutputIndex = 0xFFFFFFFFu;

groupshared float4 gs_clodVsmVertexPosition[MS_MESHLET_SIZE];
groupshared float gs_clodVsmLinearDepth[MS_MESHLET_SIZE];
#if defined(PSO_ALPHA_TEST)
groupshared float2 gs_clodVsmTexcoord[MS_MESHLET_SIZE];
#endif
groupshared uint gs_clodVsmTriangleOutputIndex[MS_MESHLET_SIZE];
groupshared uint gs_clodVsmKeptTriangleCount;
groupshared uint gs_clodVsmHasClipmapInfo;
groupshared CLodVirtualShadowClipmapInfo gs_clodVsmClipmapInfo;
#endif

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
    uint meshletLocalVertex,
    uint positionBitstreamBase,
    uint positionBitOffset,
    uint bitsX,
    uint bitsY,
    uint bitsZ,
    uint quantExp,
    int3 minQ,
    uint pagePoolSlabDescriptorIndex)
{
    uint bitsPerVertex = bitsX + bitsY + bitsZ;
    uint bitCursor = positionBitstreamBase * 8u + positionBitOffset + meshletLocalVertex * bitsPerVertex;

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

float3 DecodeCompressedNormal(uint meshletLocalVertex, uint normalArrayBase, uint vertexAttributeOffset, uint pagePoolSlabDescriptorIndex)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint addr = normalArrayBase + (vertexAttributeOffset + meshletLocalVertex) * 4u;
    uint packed = slab.Load(addr);
    return OctDecodeNormal(UnpackSnorm16x2(packed));
}

float3 DecodeCompressedColor(uint meshletLocalVertex, uint colorArrayBase, uint vertexAttributeOffset, uint pagePoolSlabDescriptorIndex)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint addr = colorArrayBase + (vertexAttributeOffset + meshletLocalVertex) * 4u;
    uint packed = slab.Load(addr);
    return float3(
        float(packed & 0xFFu) / 255.0f,
        float((packed >> 8u) & 0xFFu) / 255.0f,
        float((packed >> 16u) & 0xFFu) / 255.0f);
}

SkinningInfluences DecodePackedJoints(uint meshletLocalVertex, MeshletSetup setup)
{
    SkinningInfluences skinning;
    skinning.joints0 = uint4(0, 0, 0, 0);
    skinning.joints1 = uint4(0, 0, 0, 0);
    skinning.weights0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    skinning.weights1 = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if ((setup.pageAttributeMask & CLOD_PAGE_ATTRIBUTE_JOINTS) == 0u)
    {
        return skinning;
    }

    ByteAddressBuffer slab = ResourceDescriptorHeap[setup.pagePoolSlabDescriptorIndex];
    uint addr = setup.jointArrayBase + (setup.vertexAttributeOffset + meshletLocalVertex) * 32u;
    skinning.joints0 = LoadUint4(addr, slab);
    skinning.joints1 = LoadUint4(addr + 16u, slab);
    return skinning;
}

SkinningInfluences DecodePackedWeights(uint meshletLocalVertex, MeshletSetup setup, SkinningInfluences skinning)
{
    if ((setup.pageAttributeMask & CLOD_PAGE_ATTRIBUTE_WEIGHTS) == 0u)
    {
        return skinning;
    }

    ByteAddressBuffer slab = ResourceDescriptorHeap[setup.pagePoolSlabDescriptorIndex];
    uint addr = setup.weightArrayBase + (setup.vertexAttributeOffset + meshletLocalVertex) * 32u;
    skinning.weights0 = LoadFloat4(addr, slab);
    skinning.weights1 = LoadFloat4(addr + 16u, slab);
    return skinning;
}

void ApplyClodSkinningToVertex(uint meshletLocalVertex, MeshletSetup setup, inout Vertex vertex)
{
    if ((setup.meshBuffer.vertexFlags & VERTEX_SKINNED) == 0u)
    {
        return;
    }

    SkinningInfluences skinning = DecodePackedJoints(meshletLocalVertex, setup);
    skinning = DecodePackedWeights(meshletLocalVertex, setup, skinning);
    float4x4 skinMatrix = BuildSkinMatrix(setup.meshInstanceBuffer.skinningInstanceSlot, skinning);
    vertex.position = mul(float4(vertex.position, 1.0f), skinMatrix).xyz;
    vertex.normal = mul(vertex.normal, (float3x3)skinMatrix);
    vertex.skinning = skinning;
}

float2 DecodeCompressedUV(
    uint meshletLocalVertex,
    uint uvSetIndex,
    MeshletSetup setup)
{
    if (uvSetIndex >= setup.uvSetCount)
    {
        return float2(0.0f, 0.0f);
    }

    CLodMeshletUvDescriptor uvDesc = LoadMeshletUvDescriptorAbsolute(setup, uvSetIndex);
    uint uvBitstreamBase = LoadPageUvBitstreamBaseAbsolute(setup, uvSetIndex);
    uint uvBitsU = CLodUvDescBitsU(uvDesc);
    uint uvBitsV = CLodUvDescBitsV(uvDesc);

    uint bitsPerVertex = uvBitsU + uvBitsV;
    uint bitCursor = uvBitstreamBase * 8u + uvDesc.uvBitOffset + meshletLocalVertex * bitsPerVertex;

    ByteAddressBuffer slab = ResourceDescriptorHeap[setup.pagePoolSlabDescriptorIndex];
    uint encodedU = ReadPackedBits32_BA(slab, bitCursor, uvBitsU);
    bitCursor += uvBitsU;
    uint encodedV = ReadPackedBits32_BA(slab, bitCursor, uvBitsV);

    return float2(
        uvDesc.uvMinU + float(encodedU) * uvDesc.uvScaleU,
        uvDesc.uvMinV + float(encodedV) * uvDesc.uvScaleV);
}

VisBufferPSInput BuildVisBufferVertexAttributesForView(
    Vertex vertex,
    uint3 vGroupID,
    PerObjectBuffer objectBuffer,
    uint viewID,
    uint shadowClipmapIndex,
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
    result.shadowClipmapIndex = shadowClipmapIndex;
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
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
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
    
    float4 prevPosition = mul(prevPos, objectBuffer.prevModel);
    prevPosition = mul(prevPosition, mainCamera.prevView);
    result.prevClipPosition = mul(prevPosition, mainCamera.prevUnjitteredProjection);
    
    if (flags & VERTEX_SKINNED) {
        result.normalWorldSpace = normalize(vertex.normal);
    }
    else {
        StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::NormalMatrixBuffer)];
        float3x3 normalMatrix = (float3x3) normalMatrixBuffer[objectBuffer.normalMatrixBufferIndex];
        result.normalWorldSpace = normalize(mul(vertex.normal, normalMatrix));
    }
    
    result.color = vertex.color;
    
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
    uint shadowClipmapIndex,
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
        shadowClipmapIndex,
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
    uint shadowClipmapIndex,
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
            shadowClipmapIndex,
            clusterIndex,
            setup.meshBuffer.materialDataIndex,
            rasterInfo
        );
    }

    WriteTriangles(uGroupThreadID, setup, outputTriangles);
}

VisBufferPSInput GetVisBufferVertexAttributesForViewCLod(
    uint meshletLocalVertex,
    MeshletSetup setup,
    uint3 vGroupID,
    uint viewID,
    uint shadowClipmapIndex,
    uint clusterIndex,
    ClodViewRasterInfo rasterInfo)
{
    // Decode position and normal from per-meshlet compressed streams
    Vertex vertex = (Vertex)0;
    vertex.position = DecodeCompressedPosition(
        meshletLocalVertex,
        setup.positionBitstreamBase,
        setup.positionBitOffset,
        setup.bitsX,
        setup.bitsY,
        setup.bitsZ,
        setup.compressedPositionQuantExp,
        setup.minQ,
        setup.pagePoolSlabDescriptorIndex);
    vertex.normal = DecodeCompressedNormal(
        meshletLocalVertex,
        setup.normalArrayBase,
        setup.vertexAttributeOffset,
        setup.pagePoolSlabDescriptorIndex);
    vertex.texcoord = DecodeCompressedUV(meshletLocalVertex, 0u, setup);
    vertex.color = ((setup.pageAttributeMask & CLOD_PAGE_ATTRIBUTE_COLOR) != 0u)
        ? DecodeCompressedColor(meshletLocalVertex, setup.colorArrayBase, setup.vertexAttributeOffset, setup.pagePoolSlabDescriptorIndex)
        : float3(1.0f, 1.0f, 1.0f);
    ApplyClodSkinningToVertex(meshletLocalVertex, setup, vertex);

    return BuildVisBufferVertexAttributesForView(
        vertex,
        vGroupID,
        setup.objectBuffer,
        viewID,
        shadowClipmapIndex,
        clusterIndex,
        setup.meshBuffer.materialDataIndex,
        rasterInfo);
}

void EmitMeshletVisBufferForViewCLod(
    uint uGroupThreadID,
    MeshletSetup setup,
    uint viewID,
    uint shadowClipmapIndex,
    uint clusterIndex,
    ClodViewRasterInfo rasterInfo,
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        // Vertex i is meshlet-local (0..vertexCount-1)
        outputVertices[i] = GetVisBufferVertexAttributesForViewCLod(
            i, setup, setup.meshletIndex, viewID, shadowClipmapIndex, clusterIndex, rasterInfo);
    }

    WriteTriangles(uGroupThreadID, setup, outputTriangles);
}

struct VisibilityPerPrimitive
{
    uint triangleIndex : SV_PrimitiveID;
    //uint viewID : TEXCOORD0;
};

#if CLOD_RASTER_OUTPUT_VIRTUAL_SHADOW
void CacheMeshletVisBufferVerticesForViewCLod(
    uint uGroupThreadID,
    MeshletSetup setup,
    uint viewID,
    uint shadowClipmapIndex,
    uint clusterIndex,
    ClodViewRasterInfo rasterInfo)
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        const VisBufferPSInput vertex = GetVisBufferVertexAttributesForViewCLod(
            i, setup, setup.meshletIndex, viewID, shadowClipmapIndex, clusterIndex, rasterInfo);
        gs_clodVsmVertexPosition[i] = vertex.position;
        gs_clodVsmLinearDepth[i] = vertex.linearDepth;
#if defined(PSO_ALPHA_TEST)
        gs_clodVsmTexcoord[i] = vertex.texcoord;
#endif
    }
}

void EmitCachedMeshletVisBufferVerticesForViewCLod(
    uint uGroupThreadID,
    MeshletSetup setup,
    uint viewID,
    uint shadowClipmapIndex,
    uint clusterIndex,
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        VisBufferPSInput vertex;
        vertex.position = gs_clodVsmVertexPosition[i];
        vertex.linearDepth = gs_clodVsmLinearDepth[i];
#if defined(PSO_ALPHA_TEST)
        vertex.texcoord = gs_clodVsmTexcoord[i];
        vertex.materialDataIndex = setup.meshBuffer.materialDataIndex;
#endif
        vertex.visibleClusterIndex = clusterIndex;
        vertex.viewID = viewID;
        vertex.shadowClipmapIndex = shadowClipmapIndex;
        outputVertices[i] = vertex;
    }
}

bool ClodTriangleTouchesRenderableVirtualShadowPages(
    uint3 tri,
    ClodViewRasterInfo rasterInfo,
    CLodVirtualShadowClipmapInfo clipmapInfo,
    RWTexture2DArray<uint> pageTable)
{
    const float4 p0 = gs_clodVsmVertexPosition[tri.x];
    const float4 p1 = gs_clodVsmVertexPosition[tri.y];
    const float4 p2 = gs_clodVsmVertexPosition[tri.z];

    // Keep triangles that straddle the near plane rather than risking false rejects from
    // a bbox built from invalid post-divide coordinates.
    if (p0.w <= 0.0f || p1.w <= 0.0f || p2.w <= 0.0f)
    {
        return true;
    }

    const float visWidth = float(rasterInfo.scissorMaxX - rasterInfo.scissorMinX);
    const float visHeight = float(rasterInfo.scissorMaxY - rasterInfo.scissorMinY);
    const float scissorMinX = float(rasterInfo.scissorMinX);
    const float scissorMinY = float(rasterInfo.scissorMinY);

    const float2 ndc0 = p0.xy / p0.w;
    const float2 ndc1 = p1.xy / p1.w;
    const float2 ndc2 = p2.xy / p2.w;

    const float2 s0 = float2(
        (ndc0.x + 1.0f) * 0.5f * visWidth + scissorMinX,
        (1.0f - ndc0.y) * 0.5f * visHeight + scissorMinY);
    const float2 s1 = float2(
        (ndc1.x + 1.0f) * 0.5f * visWidth + scissorMinX,
        (1.0f - ndc1.y) * 0.5f * visHeight + scissorMinY);
    const float2 s2 = float2(
        (ndc2.x + 1.0f) * 0.5f * visWidth + scissorMinX,
        (1.0f - ndc2.y) * 0.5f * visHeight + scissorMinY);

    int2 minPx = int2(floor(min(min(s0, s1), s2)));
    int2 maxPx = int2(floor(max(max(s0, s1), s2)));

    minPx = max(minPx, int2(rasterInfo.scissorMinX, rasterInfo.scissorMinY));
    maxPx = min(maxPx, int2(int(rasterInfo.scissorMaxX) - 1, int(rasterInfo.scissorMaxY) - 1));
    minPx = max(minPx, int2(0, 0));
    maxPx = min(maxPx, int2(int(clipmapInfo.virtualResolution) - 1, int(clipmapInfo.virtualResolution) - 1));
    if (minPx.x > maxPx.x || minPx.y > maxPx.y)
    {
        return false;
    }

    return CLodVirtualShadowAnyRenderablePageInPixelRect(
        uint2(minPx),
        uint2(maxPx),
        clipmapInfo,
        pageTable);
}

void EmitFilteredMeshletTriangles(
    uint uGroupThreadID,
    MeshletSetup setup,
    out indices uint3 outputTriangles[MS_MESHLET_SIZE],
    out primitives VisibilityPerPrimitive primitiveInfo[MS_MESHLET_SIZE])
{
    const bool reverseWinding = (setup.objectBuffer.objectFlags & OBJECT_FLAG_REVERSE_WINDING) != 0;
    for (uint t = uGroupThreadID; t < setup.triCount; t += MS_THREAD_GROUP_SIZE)
    {
        const uint outputIndex = gs_clodVsmTriangleOutputIndex[t];
        if (outputIndex == kClodInvalidTriangleOutputIndex)
        {
            continue;
        }

        const uint3 tri = DecodeTriangle(t, setup);
        outputTriangles[outputIndex] = reverseWinding ? tri.xzy : tri;
        primitiveInfo[outputIndex].triangleIndex = t;
    }
}
#endif

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
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[setup.viewID];
    EmitMeshletVisBufferForView(
        uGroupThreadID,
        setup,
        setup.viewID,
        setup.shadowClipmapIndex,
        0,
        viewRasterInfo,
        outputVertices,
        outputTriangles);
    EmitPrimitiveIDs(uGroupThreadID, setup, primitiveInfo);
}

bool InitializeMeshletFromCompactedCluster(uint4 packedCluster, out MeshletSetup setup, in uint bucketMeshletIndex, in uint bucketCount)
{
    StructuredBuffer<PerMeshInstanceBuffer> meshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];

    setup.meshletIndex = CLodVisibleClusterLocalMeshletIndex(packedCluster);
    setup.meshInstanceBuffer = meshInstanceBuffer[CLodVisibleClusterInstanceID(packedCluster)];
    setup.viewID = CLodVisibleClusterViewID(packedCluster);
    setup.shadowClipmapIndex = CLodVisibleClusterShadowClipmapIndex(packedCluster);

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];

    setup.meshBuffer = perMeshBuffer[setup.meshInstanceBuffer.perMeshBufferIndex];
    setup.objectBuffer = perObjectBuffer[setup.meshInstanceBuffer.perObjectBufferIndex];

    // Use pre-resolved page address from VisibleCluster
    const uint pageSlabDesc = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
    const uint pageSlabOff  = CLodVisibleClusterPageSlabByteOffset(packedCluster);
    if (pageSlabDesc == 0)
    {
        return false;
    }

    CLodPageHeader hdr = LoadPageHeader(pageSlabDesc, pageSlabOff);

    // meshletIndex is now page-local
    if (setup.meshletIndex >= hdr.meshletCount)
    {
        return false;
    }

    // Load per-meshlet descriptor
    CLodMeshletDescriptor desc = LoadMeshletDescriptor(
        pageSlabDesc, pageSlabOff, hdr.descriptorOffset, setup.meshletIndex);

    setup.meshlet = (Meshlet)0;
    setup.vertCount = CLodDescVertexCount(desc);
    setup.triCount = CLodDescTriangleCount(desc);
    setup.vertOffset = 0;

    // Per-meshlet compression from descriptor
    setup.bitsX = CLodDescBitsX(desc);
    setup.bitsY = CLodDescBitsY(desc);
    setup.bitsZ = CLodDescBitsZ(desc);
    setup.minQ = int3(desc.minQx, desc.minQy, desc.minQz);
    setup.positionBitOffset = desc.positionBitOffset;
    setup.vertexAttributeOffset = desc.vertexAttributeOffset;
    setup.triangleByteOffset = desc.triangleByteOffset;
    setup.boneListOffset = desc.boneListOffset;
    setup.boneCount = CLodDescBoneCount(desc);
    setup.pageAttributeMask = hdr.attributeMask;
    setup.uvSetCount = hdr.uvSetCount;

    // Page-level stream base offsets (absolute in slab)
    setup.pageByteOffset = pageSlabOff;
    setup.positionBitstreamBase = pageSlabOff + hdr.positionBitstreamOffset;
    setup.normalArrayBase = pageSlabOff + hdr.normalArrayOffset;
    setup.colorArrayBase = pageSlabOff + hdr.colorArrayOffset;
    setup.jointArrayBase = pageSlabOff + hdr.jointArrayOffset;
    setup.weightArrayBase = pageSlabOff + hdr.weightArrayOffset;
    setup.uvDescriptorBase = pageSlabOff + hdr.uvDescriptorOffset;
    setup.uvBitstreamDirectoryBase = pageSlabOff + hdr.uvBitstreamDirectoryOffset;
    setup.triangleStreamBase = pageSlabOff + hdr.triangleStreamOffset;
    setup.boneIndexStreamBase = pageSlabOff + hdr.boneIndexStreamOffset;

    setup.compressedPositionQuantExp = hdr.compressedPositionQuantExp;
    setup.pagePoolSlabDescriptorIndex = pageSlabDesc;

    // Non-CLod fields unused
    setup.groupMeshletTrianglesByteOffset = 0;
    setup.postSkinningBufferOffset = 0;
    setup.prevPostSkinningBufferOffset = 0;

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

    StructuredBuffer<uint> histogram = ResourceDescriptorHeap[CLOD_RASTER_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> sortedToUnsortedMapping = ResourceDescriptorHeap[CLOD_RASTER_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX];
    uint count = histogram[bucketIndex];

    bool draw = linearizedID < count;
    uint4 packedCluster = uint4(0, 0, 0, CLOD_PACKED_VISIBLE_CLUSTER_INVALID_SHADOW_CLIPMAP_INDEX);
    MeshletSetup setup;
    uint visibleClusterIndex = baseOffset + linearizedID;
    uint unsortedClusterIndex = 0;
    uint outputVertCount = 0;
    uint outputTriCount = 0;
    ClodViewRasterInfo viewRasterInfo = (ClodViewRasterInfo)0;

    if (draw) {   
        ByteAddressBuffer compactedClusters = ResourceDescriptorHeap[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
        packedCluster = CLodLoadVisibleClusterPacked(compactedClusters, visibleClusterIndex);
        unsortedClusterIndex = sortedToUnsortedMapping[visibleClusterIndex];
        draw = InitializeMeshletFromCompactedCluster(packedCluster, setup, linearizedID, count);
    } else {
        setup.vertCount = 0;
        setup.triCount = 0;
    }

    if (draw)
    {
        viewRasterInfo = viewRasterInfoBuffer[setup.viewID];

#if CLOD_RASTER_OUTPUT_VIRTUAL_SHADOW
        if (uGroupThreadID == 0u)
        {
            gs_clodVsmKeptTriangleCount = 0u;
            gs_clodVsmHasClipmapInfo = 0u;
        }
        GroupMemoryBarrierWithGroupSync();

        CacheMeshletVisBufferVerticesForViewCLod(
            uGroupThreadID,
            setup,
            setup.viewID,
            setup.shadowClipmapIndex,
            unsortedClusterIndex,
            viewRasterInfo);
        GroupMemoryBarrierWithGroupSync();

        if (uGroupThreadID == 0u)
        {
            const uint clipmapIndex = setup.shadowClipmapIndex;
            if (clipmapIndex < kCLodVirtualShadowClipmapCount)
            {
                StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos =
                    ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX];
                const CLodVirtualShadowClipmapInfo clipmapInfo = clipmapInfos[clipmapIndex];
                if (CLodVirtualShadowClipmapIsValid(clipmapInfo) &&
                    clipmapInfo.shadowCameraBufferIndex == setup.viewID)
                {
                    gs_clodVsmHasClipmapInfo = 1u;
                    gs_clodVsmClipmapInfo = clipmapInfo;
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();

        RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX];
        for (uint t = uGroupThreadID; t < setup.triCount; t += MS_THREAD_GROUP_SIZE)
        {
            gs_clodVsmTriangleOutputIndex[t] = kClodInvalidTriangleOutputIndex;

            uint3 tri = DecodeTriangle(t, setup);
            if ((setup.objectBuffer.objectFlags & OBJECT_FLAG_REVERSE_WINDING) != 0u)
            {
                tri = tri.xzy;
            }

            if (gs_clodVsmHasClipmapInfo != 0u &&
                ClodTriangleTouchesRenderableVirtualShadowPages(
                    tri,
                    viewRasterInfo,
                    gs_clodVsmClipmapInfo,
                    pageTable))
            {
                uint outputIndex = 0u;
                InterlockedAdd(gs_clodVsmKeptTriangleCount, 1u, outputIndex);
                gs_clodVsmTriangleOutputIndex[t] = outputIndex;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        outputTriCount = gs_clodVsmKeptTriangleCount;
        outputVertCount = outputTriCount > 0u ? setup.vertCount : 0u;
#else
        outputVertCount = setup.vertCount;
        outputTriCount = setup.triCount;
#endif
    }

    SetMeshOutputCounts(outputVertCount, outputTriCount); // DXC won't accept SetMeshOutputCounts in non-uniform flow-control
    if (draw)
    {
#if CLOD_RASTER_OUTPUT_VIRTUAL_SHADOW
        if (outputVertCount != 0u)
        {
            EmitCachedMeshletVisBufferVerticesForViewCLod(
                uGroupThreadID,
                setup,
                setup.viewID,
                setup.shadowClipmapIndex,
                unsortedClusterIndex,
                outputVertices);
            EmitFilteredMeshletTriangles(uGroupThreadID, setup, outputTriangles, primitiveInfo);
        }
#else
        EmitMeshletVisBufferForViewCLod(
            uGroupThreadID,
            setup,
            setup.viewID,
            setup.shadowClipmapIndex,
            unsortedClusterIndex,
            viewRasterInfo,
            outputVertices,
            outputTriangles);
        EmitPrimitiveIDs(uGroupThreadID, setup, primitiveInfo);
#endif
    }
}
