#include "vertex.hlsli"
#include "utilities.hlsli"
#include "cbuffers.hlsli"
#include "structs.hlsli"

PSInput GetVertexAttributes(ByteAddressBuffer buffer, uint blockByteOffset, uint index, uint flags, uint vertexSize, uint3 vGroupID, PerObjectBuffer objectBuffer) {
    uint byteOffset = blockByteOffset + index * vertexSize;
    Vertex vertex = LoadVertex(byteOffset, buffer, flags);
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    float4 pos = float4(vertex.position.xyz, 1.0f);

    float4 worldPosition = mul(pos, objectBuffer.model);
    PSInput result;

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];

    if (flags & VERTEX_TEXCOORDS) {
        result.texcoord = vertex.texcoord;
    }
    
#if defined(PSO_SHADOW)
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrameBuffer.lightBufferIndex];
    LightInfo light = lights[currentLightID];
    matrix lightMatrix;
    switch(light.type) {
        case 0: { // Point light
            StructuredBuffer<unsigned int> pointLightCubemapIndicesBuffer = ResourceDescriptorHeap[perFrameBuffer.pointLightCubemapBufferIndex];
            uint lightCameraIndex = pointLightCubemapIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            break;
        }
        case 1: { // Spot light
            StructuredBuffer<unsigned int> spotLightCubemapIndicesBuffer = ResourceDescriptorHeap[perFrameBuffer.spotLightMatrixBufferIndex];
            uint lightCameraIndex = spotLightCubemapIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            break;
        }
        case 2: { // Directional light
            StructuredBuffer<unsigned int> directionalLightCascadeIndicesBuffer = ResourceDescriptorHeap[perFrameBuffer.directionalLightCascadeBufferIndex];
            uint lightCameraIndex = directionalLightCascadeIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            break;
        }
    }
    result.position = mul(worldPosition, lightMatrix);
    return result;
#endif // SHADOW
    
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    result.positionWorldSpace = worldPosition;
    float4 viewPosition = mul(worldPosition, mainCamera.view);
    result.positionViewSpace = viewPosition;
    result.position = mul(viewPosition, mainCamera.projection);
    
    if (flags & VERTEX_SKINNED) {
        result.normalWorldSpace = normalize(vertex.normal);
    }
    else {
        StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[normalMatrixBufferDescriptorIndex];
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

[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void MSMain(
    const uint uGroupThreadID : SV_GroupThreadID,
    const uint3 vGroupID : SV_GroupID,
    out vertices PSInput outputVertices[64],
    out indices uint3 outputTriangles[64]) {

    // Test if this meshlet is culled
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[perMeshInstanceBufferDescriptorIndex];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    StructuredBuffer<bool> meshletCullingBitfieldBuffer = ResourceDescriptorHeap[meshletCullingBitfieldBufferDescriptorIndex];
    unsigned int meshletBitfieldIndex = meshInstanceBuffer.meshletBoundsBufferStartIndex + vGroupID.x;
    
    bool bCulled = meshletCullingBitfieldBuffer[meshletBitfieldIndex];
    
    
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[postSkinningVertexBufferDescriptorIndex]; // Base vertex buffer
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[meshletBufferDescriptorIndex]; // Meshlets, containing vertex & primitive offset & num
    StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[meshletVerticesBufferDescriptorIndex]; // Meshlet vertices, as indices into base vertex buffer
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[meshletTrianglesBufferDescriptorIndex]; // meshlet triangles, as local offsets from the current vertex_offset, indexing into meshletVerticesBuffer
    
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[perObjectBufferDescriptorIndex];
    PerObjectBuffer objectBuffer = perObjectBuffer[perObjectBufferIndex];
    
    uint meshletOffset = meshBuffer.meshletBufferOffset;
    Meshlet meshlet = meshletBuffer[meshletOffset+vGroupID.x];
    
    uint vertCount = 0;
    uint triCount = 0;
    if (!bCulled)
    {
        vertCount = meshlet.VertCount;
        triCount = meshlet.TriCount;
    }
    SetMeshOutputCounts(vertCount, triCount);
    if (bCulled)
    {
        return;
    }
    
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
    
    if (uGroupThreadID < meshlet.VertCount) {
        //uint thisVertex = meshletVerticesBuffer[vertOffset + uGroupThreadID];
        outputVertices[uGroupThreadID] = GetVertexAttributes(vertexBuffer, meshInstanceBuffer.postSkinningVertexBufferOffset, vertOffset + uGroupThreadID, meshBuffer.vertexFlags, meshBuffer.vertexByteSize, vGroupID, objectBuffer);
    }
    if (uGroupThreadID < meshlet.TriCount) {
        outputTriangles[uGroupThreadID] = meshletIndices;
    }
}