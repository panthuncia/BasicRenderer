#include "vertex.hlsli"
#include "utilities.hlsli"
#include "cbuffers.hlsli"
#include "structs.hlsli"

MeshVertex LoadVertex(uint byteOffset, ByteAddressBuffer buffer, uint flags) {
    MeshVertex vertex;

    // Load position (float3, 12 bytes)
    vertex.position = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    // Load normal (float3, 12 bytes)
    vertex.normal = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    if (flags & VERTEX_TEXCOORDS) {
        // Load texcoord (float2, 8 bytes)
        vertex.texcoord = LoadFloat2(byteOffset, buffer);
        byteOffset += 8;
    }

    if (flags & VERTEX_TANBIT) {
        // Load tangent (float3, 12 bytes)
        vertex.tangent = LoadFloat3(byteOffset, buffer);
        byteOffset += 12;

        // Load bitangent (float3, 12 bytes)
        vertex.bitangent = LoadFloat3(byteOffset, buffer);
        byteOffset += 12;
    }

    if (flags & VERTEX_SKINNED) {
        // Load joints (uint4, 16 bytes)
        vertex.joints = LoadUint4(byteOffset, buffer);
        byteOffset += 16;

        // Load weights (float4, 16 bytes)
        vertex.weights = LoadFloat4(byteOffset, buffer);
        byteOffset += 16;
    }

    return vertex;
}

PSInput GetVertexAttributes(ByteAddressBuffer buffer, uint blockByteOffset, uint index, uint flags, uint vertexSize, uint3 vGroupID) {
    uint byteOffset = blockByteOffset + index * vertexSize;
    MeshVertex vertex = LoadVertex(byteOffset, buffer, flags);
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    float4 pos = float4(vertex.position.xyz, 1.0f);

    float3x3 normalMatrixSkinnedIfNecessary = (float3x3) normalMatrix;
    
    #if defined(PSO_SKINNED)
    StructuredBuffer<float4> boneTransformsBuffer = ResourceDescriptorHeap[boneTransformBufferIndex];
    StructuredBuffer<float4> inverseBindMatricesBuffer = ResourceDescriptorHeap[inverseBindMatricesBufferIndex];
    
    matrix bone1 = loadMatrixFromBuffer(boneTransformsBuffer, vertex.joints.x);
    matrix bone2 = loadMatrixFromBuffer(boneTransformsBuffer, vertex.joints.y);
    matrix bone3 = loadMatrixFromBuffer(boneTransformsBuffer, vertex.joints.z);
    matrix bone4 = loadMatrixFromBuffer(boneTransformsBuffer, vertex.joints.w);
    
    matrix bindMatrix1 = loadMatrixFromBuffer(inverseBindMatricesBuffer, vertex.joints.x);
    matrix bindMatrix2 = loadMatrixFromBuffer(inverseBindMatricesBuffer, vertex.joints.y);
    matrix bindMatrix3 = loadMatrixFromBuffer(inverseBindMatricesBuffer, vertex.joints.z);
    matrix bindMatrix4 = loadMatrixFromBuffer(inverseBindMatricesBuffer, vertex.joints.w);

    matrix skinMatrix = vertex.weights.x * mul(bindMatrix1, bone1) +
                        vertex.weights.y * mul(bindMatrix2, bone2) +
                        vertex.weights.z * mul(bindMatrix3, bone3) +
                        vertex.weights.w * mul(bindMatrix4, bone4);
    
    pos = mul(pos, skinMatrix);
    normalMatrixSkinnedIfNecessary = mul(normalMatrixSkinnedIfNecessary, (float3x3)skinMatrix);
#endif // SKINNED
    
    float4 worldPosition = mul(pos, model);
    PSInput result;
    
    #if defined(PSO_SHADOW)
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrameBuffer.lightBufferIndex];
    LightInfo light = lights[currentLightID];
    matrix lightMatrix;
    switch(light.type) {
        case 0: { // Point light
            StructuredBuffer<float4> pointLightCubemapBuffer = ResourceDescriptorHeap[perFrameBuffer.pointLightCubemapBufferIndex];
            lightMatrix = loadMatrixFromBuffer(pointLightCubemapBuffer, lightViewIndex);
            break;
        }
        case 1: { // Spot light
            StructuredBuffer<float4> spotLightMatrixBuffer = ResourceDescriptorHeap[perFrameBuffer.spotLightMatrixBufferIndex];
            lightMatrix = loadMatrixFromBuffer(spotLightMatrixBuffer, lightViewIndex);
            break;
        }
        case 2: { // Directional light
            StructuredBuffer<float4> directionalLightCascadeBuffer = ResourceDescriptorHeap[perFrameBuffer.directionalLightCascadeBufferIndex];
            lightMatrix = loadMatrixFromBuffer(directionalLightCascadeBuffer, lightViewIndex);
            break;
        }
    }
    result.position = mul(worldPosition, lightMatrix);
    return result;
#endif // SHADOW
    
    result.positionWorldSpace = worldPosition;
    float4 viewPosition = mul(worldPosition, perFrameBuffer.view);
    result.positionViewSpace = viewPosition;
    result.position = mul(viewPosition, perFrameBuffer.projection);
    
    result.normalWorldSpace = normalize(mul(vertex.normal, normalMatrixSkinnedIfNecessary));
    
#if defined(PSO_NORMAL_MAP) || defined(PSO_PARALLAX)
    result.TBN_T = normalize(mul(vertex.tangent, normalMatrixSkinnedIfNecessary));
    result.TBN_B = normalize(mul(vertex.bitangent, normalMatrixSkinnedIfNecessary));
    result.TBN_N = normalize(mul(vertex.normal, normalMatrixSkinnedIfNecessary));
#endif // NORMAL_MAP
    
#if defined(PSO_VERTEX_COLORS)
    result.color = vertex.color;
#endif
#if defined(PSO_TEXTURED)
    result.texcoord = vertex.texcoord;
#endif
    result.meshletIndex = vGroupID.x;
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

    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[vertexBufferIndex]; // Base vertex buffer
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[meshletBufferIndex]; // Meshlets, containing vertex & primitive offset & num
    StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[meshletVerticesBufferIndex]; // Meshlet vertices, as indices into base vertex buffer
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[meshletTrianglesBufferIndex]; // meshlet triangles, as local offsets from the current vertex_offset, indexing into meshletVerticesBuffer
    
    Meshlet meshlet = meshletBuffer[meshletBufferOffset+vGroupID.x];
    SetMeshOutputCounts(meshlet.VertCount, meshlet.TriCount);
    
    uint triOffset = meshletTrianglesBufferOffset+meshlet.TriOffset + uGroupThreadID * 3;
    uint3 meshletIndices = uint3(LoadByte(meshletTrianglesBuffer, triOffset), LoadByte(meshletTrianglesBuffer, triOffset + 1), LoadByte(meshletTrianglesBuffer, triOffset+2)); // Local indices into meshletVerticesBuffer
    uint vertOffset = meshlet.VertOffset + meshletVerticesBufferOffset;
    //uint3 vertexIndices = uint3(meshletVerticesBuffer[vertOffset + meshletIndices.x], meshletVerticesBuffer[vertOffset + meshletIndices.y], meshletVerticesBuffer[vertOffset + meshletIndices.z]); // Global indices into vertexBuffer
    if (uGroupThreadID < meshlet.VertCount) {
        uint thisVertex = meshletVerticesBuffer[vertOffset + uGroupThreadID];
        outputVertices[uGroupThreadID] = GetVertexAttributes(vertexBuffer, vertexBufferOffset, thisVertex, vertexFlags, vertexByteSize, vGroupID);
    }
    if (uGroupThreadID < meshlet.TriCount) {
        outputTriangles[uGroupThreadID] = meshletIndices;
    }
}