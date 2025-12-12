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

VisBufferPSInput GetVisBufferVertexAttributes(
ByteAddressBuffer buffer, 
uint blockByteOffset, 
uint index, 
uint flags, 
uint vertexSize, 
uint3 vGroupID, 
PerObjectBuffer objectBuffer, 
uint clusterToVisibleClusterTableStartIndex)
{
    uint byteOffset = blockByteOffset + index * vertexSize;
    Vertex vertex = LoadVertex(byteOffset, buffer, flags);
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    float4 pos = float4(vertex.position.xyz, 1.0f);
    float4 worldPosition = mul(pos, objectBuffer.model);
    float4 viewPosition = mul(worldPosition, mainCamera.view);
    
    StructuredBuffer<uint> clusterToVisibleClusterTable = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer)];
    uint clusterIndex = clusterToVisibleClusterTable[clusterToVisibleClusterTableStartIndex + vGroupID.x];
    
    VisBufferPSInput result;
    result.position = mul(viewPosition, mainCamera.projection);
    result.visibleClusterTableIndex = clusterIndex;
    result.linearDepth = -viewPosition.z;
    
#if defined(PSO_ALPHA_TEST)
    result.texcoord = vertex.texcoord;
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
void EmitMeshletVisBuffer(uint uGroupThreadID, MeshletSetup setup, out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE], out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    for (uint i = uGroupThreadID; i < setup.vertCount; i += MS_THREAD_GROUP_SIZE)
    {
        // Which meshlet-local triangle ID is this?
        outputVertices[i] = GetVisBufferVertexAttributes(
            setup.vertexBuffer,
            setup.postSkinningBufferOffset,
            setup.vertOffset + i,
            setup.meshBuffer.vertexFlags,
            setup.meshBuffer.vertexByteSize,
            setup.meshletIndex,
            setup.objectBuffer,
            setup.meshInstanceBuffer.clusterToVisibleClusterTableStartIndex
        );
    }

    WriteTriangles(uGroupThreadID, setup, outputTriangles);
}

struct VisibilityPerPrimitive
{
    uint triangleIndex : SV_PrimitiveID;
};

void EmitPrimitiveIDs(uint uGroupThreadID, MeshletSetup setup, out primitives VisibilityPerPrimitive primitiveInfo[MS_MESHLET_SIZE])
{
    for (uint t = uGroupThreadID; t < setup.triCount; t += MS_THREAD_GROUP_SIZE)
    {
        primitiveInfo[t].triangleIndex = t;
    }
}

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
    EmitMeshletVisBuffer(uGroupThreadID, setup, outputVertices, outputTriangles);
    EmitPrimitiveIDs(uGroupThreadID, setup, primitiveInfo);
}