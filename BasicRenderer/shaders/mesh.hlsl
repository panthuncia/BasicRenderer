#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/loadingUtils.hlsli"
#include "Common/defines.h"
#include "include/meshletPayload.hlsli"
#include "Include/meshletCommon.hlsli"

PSInput GetVertexAttributes(ByteAddressBuffer buffer, uint blockByteOffset, uint prevBlockByteOffset, uint index, uint flags, uint vertexSize, uint3 vGroupID, PerObjectBuffer objectBuffer) {
    uint byteOffset = blockByteOffset + index * vertexSize;
    Vertex vertex = LoadVertex(byteOffset, buffer, flags);
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    float4 pos = float4(vertex.position.xyz, 1.0f);
    float4 prevPos;
    if (flags & VERTEX_SKINNED)
    {
        uint prevByteOffset = prevBlockByteOffset + index * vertexSize;
        prevPos = float4(LoadFloat3(prevByteOffset, buffer), 1.0);
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
    ByteAddressBuffer buffer,
    uint blockByteOffset,
    uint index,
    uint flags,
    uint vertexSize,
    uint3 vGroupID,
    PerObjectBuffer objectBuffer,
    uint viewID,
    uint clusterIndex,
    uint materialDataIndex)
{
    uint byteOffset = blockByteOffset + index * vertexSize;
    Vertex vertex = LoadVertex(byteOffset, buffer, flags);

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera viewCamera = cameras[viewID];

    float4 pos = float4(vertex.position.xyz, 1.0f);
    float4 worldPosition = mul(pos, objectBuffer.model);
    float4 viewPosition = mul(worldPosition, viewCamera.view);

    VisBufferPSInput result;
    result.position = mul(viewPosition, viewCamera.projection);
    result.visibleClusterIndex = clusterIndex;
    result.linearDepth = -viewPosition.z;
    result.viewID = viewID;
#if defined(PSO_ALPHA_TEST)
    result.texcoord = vertex.texcoord;
    result.materialDataIndex = materialDataIndex;
#endif

    return result;
}

void WriteTriangles(uint uGroupThreadID, MeshletSetup setup, out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint t = uGroupThreadID; t < setup.triCount; t += MS_THREAD_GROUP_SIZE)
    {
        outputTriangles[t] = DecodeTriangle(t, setup);
    }
}

// Emit for GBuffer path
void EmitMeshletGBuffer(uint uGroupThreadID, MeshletSetup setup, out vertices PSInput outputVertices[MS_MESHLET_SIZE], out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        outputVertices[i] = GetVertexAttributes(
            setup.vertexBuffer,
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
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        outputVertices[i] = GetVisBufferVertexAttributesForView(
            setup.vertexBuffer,
            setup.postSkinningBufferOffset,
            setup.vertOffset + i,
            setup.meshBuffer.vertexFlags,
            setup.meshBuffer.vertexByteSize,
            setup.meshletIndex,
            setup.objectBuffer,
            viewID,
            clusterIndex,
            setup.meshBuffer.materialDataIndex
        );
    }

    WriteTriangles(uGroupThreadID, setup, outputTriangles);
}

VisBufferPSInput GetVisBufferVertexAttributesForViewIndexed(
    ByteAddressBuffer buffer,
    StructuredBuffer<uint> meshletVerticesBuffer,
    uint meshletVerticesBaseOffset,
    uint meshletVertexIndex,
    uint blockByteOffset,
    uint flags,
    uint vertexSize,
    uint3 vGroupID,
    PerObjectBuffer objectBuffer,
    uint viewID,
    uint clusterIndex,
    uint materialDataIndex)
{
    uint vertexIndex = meshletVerticesBuffer[meshletVerticesBaseOffset + meshletVertexIndex];
    return GetVisBufferVertexAttributesForView(
        buffer,
        blockByteOffset,
        vertexIndex,
        flags,
        vertexSize,
        vGroupID,
        objectBuffer,
        viewID,
        clusterIndex,
        materialDataIndex);
}

void EmitMeshletVisBufferForViewIndexed(
    uint uGroupThreadID,
    MeshletSetup setup,
    uint viewID,
    uint clusterIndex,
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        uint meshletVertexIndex = setup.vertOffset + i;
        outputVertices[i] = GetVisBufferVertexAttributesForViewIndexed(
            setup.vertexBuffer,
            setup.meshletVerticesBuffer,
            setup.meshBuffer.clodMeshletVerticesBufferOffset,
            meshletVertexIndex,
            setup.postSkinningBufferOffset,
            setup.meshBuffer.vertexFlags,
            setup.meshBuffer.vertexByteSize,
            setup.meshletIndex,
            setup.objectBuffer,
            viewID,
            clusterIndex,
            setup.meshBuffer.materialDataIndex
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
    EmitMeshletVisBufferForView(uGroupThreadID, setup, setup.viewID, 0, outputVertices, outputTriangles); // TODO: broken
    EmitPrimitiveIDs(uGroupThreadID, setup, primitiveInfo);
}

#include "PerPassRootConstants/clodRootConstants.h"

bool InitializeMeshletFromCompactedCluster(VisibleCluster cluster, out MeshletSetup setup, in uint bucketMeshletIndex, in uint bucketCount)
{
	if (bucketMeshletIndex >= bucketCount)
    {
        setup.vertCount = 0;
        setup.triCount = 0;
        return false;
    }	

    StructuredBuffer<PerMeshInstanceBuffer> meshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];

    setup.meshletIndex = cluster.meshletID;
    setup.meshInstanceBuffer = meshInstanceBuffer[cluster.instanceID];
    setup.viewID = cluster.viewID;

    // TODO: Fetch only necessary data for visibility buffer
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletOffsets)];
    StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletVertexIndices)];
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletTriangles)];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    setup.meshBuffer = perMeshBuffer[setup.meshInstanceBuffer.perMeshBufferIndex];
    setup.objectBuffer = perObjectBuffer[setup.meshInstanceBuffer.perObjectBufferIndex];

    uint meshletOffset = setup.meshBuffer.clodMeshletBufferOffset;
    setup.meshlet = meshletBuffer[meshletOffset + setup.meshletIndex];

    setup.vertCount = setup.meshlet.VertCount;
    setup.triCount = setup.meshlet.TriCount;
    setup.vertOffset = setup.meshlet.VertOffset;

    setup.vertexBuffer = vertexBuffer;
    setup.meshletTrianglesBuffer = meshletTrianglesBuffer;
    setup.meshletVerticesBuffer = meshletVerticesBuffer;

    uint postSkinningBase = setup.meshInstanceBuffer.postSkinningVertexBufferOffset;
    setup.postSkinningBufferOffset = postSkinningBase;
    setup.prevPostSkinningBufferOffset = postSkinningBase;

    if (setup.meshBuffer.vertexFlags & VERTEX_SKINNED)
    {
        uint stride = setup.meshBuffer.vertexByteSize * setup.meshBuffer.numVertices;
        setup.postSkinningBufferOffset += stride * (perFrameBuffer.frameIndex % 2);
        setup.prevPostSkinningBufferOffset += stride * ((perFrameBuffer.frameIndex + 1) % 2);
    }

    if (setup.meshletIndex >= setup.meshBuffer.clodNumMeshlets)
    {
        return false;
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
    uint count = histogram[bucketIndex];

    StructuredBuffer<VisibleCluster> compactedClusters = ResourceDescriptorHeap[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
    uint visibleClusterIndex = baseOffset + linearizedID;
    VisibleCluster cluster = compactedClusters[visibleClusterIndex];

    MeshletSetup setup;
    bool draw = InitializeMeshletFromCompactedCluster(cluster, setup, linearizedID, count);

    SetMeshOutputCounts(setup.vertCount, setup.triCount);
    if (draw)
    {
        EmitMeshletVisBufferForViewIndexed(uGroupThreadID, setup, setup.viewID, visibleClusterIndex, outputVertices, outputTriangles);
        EmitPrimitiveIDs(uGroupThreadID, setup, primitiveInfo);
    }
}