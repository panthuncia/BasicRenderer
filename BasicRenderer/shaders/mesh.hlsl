#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/loadingUtils.hlsli"
#include "Common/defines.h"

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

struct Payload
{
    uint MeshletIndices[AS_GROUP_SIZE];
};
groupshared Payload s_Payload;
[NumThreads(AS_GROUP_SIZE, 1, 1)]
void ASMain(uint uGroupThreadID : SV_GroupThreadID, uint uDispatchThreadID : SV_DispatchThreadID, uint uGroupID : SV_GroupID)
{
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    
    ByteAddressBuffer meshletCullingBitfieldBuffer = ResourceDescriptorHeap[meshletCullingBitfieldBufferDescriptorIndex];
    unsigned int meshletBitfieldIndex = meshInstanceBuffer.meshletBitfieldStartIndex + uDispatchThreadID;
 
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    // Culling handled in compute shader
    bool visible = uDispatchThreadID < meshBuffer.numMeshlets && !GetBit(meshletCullingBitfieldBuffer, meshletBitfieldIndex);

    if (visible) {
        uint index = WavePrefixCountBits(visible);
        s_Payload.MeshletIndices[index] = uDispatchThreadID;
    }
    
    uint visibleCount = WaveActiveCountBits(visible);
    DispatchMesh(visibleCount, 1, 1, s_Payload);
}

[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void MSMain(
    const uint uGroupThreadID : SV_GroupThreadID,
    const uint vGroupID : SV_GroupID,
    in payload Payload payload,
    out vertices PSInput outputVertices[64],
    out indices uint3 outputTriangles[64]) {

    uint meshletIndex = payload.MeshletIndices[vGroupID];
    
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    if (meshletIndex >= meshBuffer.numMeshlets) // Can this happen?
    {
        return;
    }
    
    // Test if this meshlet is culled
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    ByteAddressBuffer meshletCullingBitfieldBuffer = ResourceDescriptorHeap[meshletCullingBitfieldBufferDescriptorIndex];
    unsigned int meshletBitfieldIndex = meshInstanceBuffer.meshletBitfieldStartIndex + meshletIndex;
    
    //bool bCulled = GetBit(meshletCullingBitfieldBuffer, meshletBitfieldIndex);
    
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)]; // Base vertex buffer
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletOffsets)]; // Meshlets, containing vertex & primitive offset & num
    StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletVertexIndices)]; // Meshlet vertices, as indices into base vertex buffer
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletTriangles)]; // meshlet triangles, as local offsets from the current vertex_offset, indexing into meshletVerticesBuffer
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    PerObjectBuffer objectBuffer = perObjectBuffer[perObjectBufferIndex];
    
    uint meshletOffset = meshBuffer.meshletBufferOffset;
    Meshlet meshlet = meshletBuffer[meshletOffset + meshletIndex];
    
    uint vertCount = 0;
    uint triCount = 0;
    //if (!bCulled)
    //{
        vertCount = meshlet.VertCount;
        triCount = meshlet.TriCount;
    //}
    SetMeshOutputCounts(vertCount, triCount);
    //if (bCulled) // Early out works in debug mode, but compiler returns a null shader without error in release mode
    //{
    //    return;
    //}
    
    uint triOffset = meshBuffer.meshletTrianglesBufferOffset + meshlet.TriOffset + uGroupThreadID * 3;
    
    
    //uint3 meshletIndices = uint3(LoadByte(meshletTrianglesBuffer, triOffset), LoadByte(meshletTrianglesBuffer, triOffset + 1), LoadByte(meshletTrianglesBuffer, triOffset+2)); // Local indices into meshletVerticesBuffer
    uint alignedOffset = (triOffset / 4) * 4;
    uint firstWord = meshletTrianglesBuffer.Load(alignedOffset);

    // Calculate the starting byte offset within firstWord
    uint byteOffset = triOffset % 4;

    // Extract the first byte
    uint b0 = (firstWord >> (byteOffset * 8)) & 0xFF;

    // For the second and third bytes, we may still be within the same 4-byte word or we may cross over.
    uint b1, b2;

    if (byteOffset <= 1) {
        // All three bytes are within the same word
        b1 = (firstWord >> ((byteOffset + 1) * 8)) & 0xFF;
        b2 = (firstWord >> ((byteOffset + 2) * 8)) & 0xFF;
    } else if (byteOffset == 2) {
        // The second byte is in this word, but the third byte spills into the next word
        b1 = (firstWord >> ((byteOffset + 1) * 8)) & 0xFF;
        uint secondWord = meshletTrianglesBuffer.Load(alignedOffset + 4);
        b2 = secondWord & 0xFF; // The first byte of the next word
    } else {
        // byteOffset == 3
        // The first byte is at the last position in firstWord,
        // The next two bytes must come from the next word.
        uint secondWord = meshletTrianglesBuffer.Load(alignedOffset + 4);
        b1 = secondWord & 0xFF;               // first byte of the next word
        b2 = (secondWord >> 8) & 0xFF;        // second byte of the next word
    }

    uint3 meshletIndices = uint3(b0, b1, b2);
    
    uint vertOffset = meshlet.VertOffset;
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    uint postSkinningBufferOffset = meshInstanceBuffer.postSkinningVertexBufferOffset;
    
    uint prevPostSkinningBufferOffset = postSkinningBufferOffset;
    if (meshBuffer.vertexFlags & VERTEX_SKINNED)
    {
        postSkinningBufferOffset += meshBuffer.vertexByteSize * meshBuffer.numVertices * (perFrameBuffer.frameIndex % 2);
        prevPostSkinningBufferOffset += meshBuffer.vertexByteSize * meshBuffer.numVertices * ((perFrameBuffer.frameIndex + 1) % 2);
    }
    
    if (uGroupThreadID < meshlet.VertCount) {
        //uint thisVertex = meshletVerticesBuffer[vertOffset + uGroupThreadID];
        outputVertices[uGroupThreadID] = GetVertexAttributes(vertexBuffer, postSkinningBufferOffset, prevPostSkinningBufferOffset, vertOffset + uGroupThreadID, meshBuffer.vertexFlags, meshBuffer.vertexByteSize, meshletIndex, objectBuffer);
    }
    if (uGroupThreadID < meshlet.TriCount) {
        outputTriangles[uGroupThreadID] = meshletIndices;
    }
}