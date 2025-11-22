#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/loadingUtils.hlsli"
#include "Common/defines.h"
#include "include/meshletPayload.hlsli"

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


VisBufferPSInput GetVisBufferVertexAttributes(ByteAddressBuffer buffer, uint blockByteOffset, uint index, uint flags, uint vertexSize, uint3 vGroupID) {
    uint byteOffset = blockByteOffset + index * vertexSize;
    Vertex vertex = LoadVertex(byteOffset, buffer, flags);
    
    VisBufferPSInput result;
    result.position = float4(vertex.position.xyz, 1.0f);
    result.meshletIndex = vGroupID.x;
    result.meshletVertexIndex = index;
    
    return result;
}

Meshlet loadMeshlet(uint4 raw) {
    Meshlet m;
    m.VertOffset = raw.x;
    m.TriOffset = raw.y;
    m.VertCount = raw.z;
    m.TriCount = raw.w;
    return m;
}

uint LoadByte(ByteAddressBuffer buffer, uint byteIndex) {
    // Calculate the 4-byte aligned offset to load from the buffer
    uint alignedOffset = (byteIndex / 4) * 4;

    // Load the full 4-byte word containing the byte we're interested in
    uint word = buffer.Load(alignedOffset);

    // Calculate which byte within the word we need (0-3)
    uint byteOffset = byteIndex % 4;

    // Extract the byte by shifting and masking
    uint byteValue = (word >> (byteOffset * 8)) & 0xFF;

    return byteValue;
}

struct MeshletSetup
{
    uint meshletIndex;
    Meshlet meshlet;
    PerMeshBuffer meshBuffer;
    PerMeshInstanceBuffer meshInstanceBuffer;
    PerObjectBuffer objectBuffer;
    uint vertCount;
    uint triCount;
    uint vertOffset;
    uint postSkinningBufferOffset;
    uint prevPostSkinningBufferOffset;
    ByteAddressBuffer vertexBuffer;
    ByteAddressBuffer meshletTrianglesBuffer;
    StructuredBuffer<uint> meshletVerticesBuffer;
};

bool InitializeMeshlet(
    in uint meshletIndex,
    out MeshletSetup setup)
{
    setup.meshletIndex = meshletIndex;
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    setup.meshBuffer = perMeshBuffer[perMeshBufferIndex];

    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    setup.meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];

    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletOffsets)];
    StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletVertexIndices)];
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletTriangles)];

    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    setup.objectBuffer = perObjectBuffer[perObjectBufferIndex];

    uint meshletOffset = setup.meshBuffer.meshletBufferOffset;
    setup.meshlet = meshletBuffer[meshletOffset + setup.meshletIndex];

    setup.vertCount = setup.meshlet.VertCount;
    setup.triCount = setup.meshlet.TriCount;
    
    SetMeshOutputCounts(setup.vertCount, setup.triCount); // This always needs to be set
    
    if (setup.meshletIndex >= setup.meshBuffer.numMeshlets)
    {
        return false;
    }
    
    setup.vertOffset = setup.meshlet.VertOffset;

    setup.vertexBuffer = vertexBuffer;
    setup.meshletTrianglesBuffer = meshletTrianglesBuffer;
    setup.meshletVerticesBuffer = meshletVerticesBuffer;

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    uint postSkinningBase = setup.meshInstanceBuffer.postSkinningVertexBufferOffset;

    setup.postSkinningBufferOffset = postSkinningBase;
    setup.prevPostSkinningBufferOffset = postSkinningBase;

    if (setup.meshBuffer.vertexFlags & VERTEX_SKINNED)
    {
        uint stride = setup.meshBuffer.vertexByteSize * setup.meshBuffer.numVertices;
        setup.postSkinningBufferOffset += stride * (perFrameBuffer.frameIndex % 2);
        setup.prevPostSkinningBufferOffset += stride * ((perFrameBuffer.frameIndex + 1) % 2);
    }

    return true;
}

uint3 DecodeTriangle(uint triLocalIndex, MeshletSetup setup)
{
    uint triOffset = setup.meshBuffer.meshletTrianglesBufferOffset + setup.meshlet.TriOffset + triLocalIndex * 3;
    uint alignedOffset = (triOffset / 4) * 4;
    uint firstWord = setup.meshletTrianglesBuffer.Load(alignedOffset);
    uint byteOffset = triOffset % 4;

    // Extract the first byte
    uint b0 = (firstWord >> (byteOffset * 8)) & 0xFF;
    // For the second and third bytes, we may still be within the same 4-byte word or we may cross over.
    uint b1, b2;

    if (byteOffset <= 1)
    {
        // All three bytes are within the same word
        b1 = (firstWord >> ((byteOffset + 1) * 8)) & 0xFF;
        b2 = (firstWord >> ((byteOffset + 2) * 8)) & 0xFF;
    }
    else if (byteOffset == 2)
    {
        // The second byte is in this word, but the third byte spills into the next word
        b1 = (firstWord >> ((byteOffset + 1) * 8)) & 0xFF;
        uint secondWord = setup.meshletTrianglesBuffer.Load(alignedOffset + 4);
        b2 = secondWord & 0xFF;
    }
    else
    { // byteOffset == 3
        // The first byte is at the last position in firstWord,
        // The next two bytes must come from the next word.
        uint secondWord = setup.meshletTrianglesBuffer.Load(alignedOffset + 4);
        b1 = secondWord & 0xFF;
        b2 = (secondWord >> 8) & 0xFF;
    }

    return uint3(b0, b1, b2);
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
        outputVertices[i] = GetVisBufferVertexAttributes(
            setup.vertexBuffer,
            setup.postSkinningBufferOffset,
            setup.vertOffset + i,
            setup.meshBuffer.vertexFlags,
            setup.meshBuffer.vertexByteSize,
            setup.meshletIndex);
    }

    WriteTriangles(uGroupThreadID, setup, outputTriangles);
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
    if (!InitializeMeshlet(meshletIndex, setup))
    {
        return;
    }
    EmitMeshletGBuffer(uGroupThreadID, setup, outputVertices, outputTriangles);
}

[outputtopology("triangle")]
[numthreads(MS_THREAD_GROUP_SIZE, 1, 1)]
void VisBufferMSMain(
    const uint uGroupThreadID : SV_GroupThreadID,
    const uint vGroupID : SV_GroupID,
    in payload Payload payload,
    out vertices VisBufferPSInput outputVertices[MS_MESHLET_SIZE],
    out indices uint3 outputTriangles[MS_MESHLET_SIZE])
{
    uint meshletIndex = payload.MeshletIndices[vGroupID];
    MeshletSetup setup;
    if (!InitializeMeshlet(meshletIndex, setup))
    {
        return;
    }
    EmitMeshletVisBuffer(uGroupThreadID, setup, outputVertices, outputTriangles);
}