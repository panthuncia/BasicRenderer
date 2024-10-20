#include "vertex.hlsli"
#include "utilities.hlsli"
#include "cbuffers.hlsli"
#include "structs.hlsli"
struct Meshlet {
    uint VertOffset;
    uint TriOffset;
    uint VertCount;
    uint TriCount;
};

cbuffer BufferIndices : register(b5) {
    uint vertexBufferIndex;
    uint vertexBufferOffset;
    uint meshletBufferIndex;
    uint meshletVerticesBufferIndex;
    uint meshletTrianglesBufferIndex;
}

Vertex LoadVertex(uint byteOffset, ByteAddressBuffer buffer) {
    Vertex vertex;

    // Load position (float3, 12 bytes)
    vertex.position = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    // Load normal (float3, 12 bytes)
    vertex.normal = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

#if defined(TEXTURED)
    // Load texcoord (float2, 8 bytes)
    vertex.texcoord = LoadFloat2(byteOffset, buffer);
    byteOffset += 8;
#endif

#if defined(NORMAL_MAP) || defined(PARALLAX)
    // Load tangent (float3, 12 bytes)
    vertex.tangent = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    // Load bitangent (float3, 12 bytes)
    vertex.bitangent = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;
#endif

#if defined(SKINNED)
    // Load joints (uint4, 16 bytes)
    vertex.joints = LoadUint4(byteOffset, buffer);
    byteOffset += 16;

    // Load weights (float4, 16 bytes)
    vertex.weights = LoadFloat4(byteOffset, buffer);
    byteOffset += 16;
#endif

    return vertex;
}

PSInput GetVertexAttributes(ByteAddressBuffer buffer, uint index) {
    uint byteOffset = index * 64; // 64 bytes per vertex
    Vertex vertex = LoadVertex(byteOffset, buffer);
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    float4 pos = float4(vertex.position.xyz, 1.0f);

    float3x3 normalMatrixSkinnedIfNecessary = (float3x3) normalMatrix;
    
    #if defined(SKINNED)
    StructuredBuffer<float4> boneTransformsBuffer = ResourceDescriptorHeap[boneTransformBufferIndex];
    StructuredBuffer<float4> inverseBindMatricesBuffer = ResourceDescriptorHeap[inverseBindMatricesBufferIndex];
    
    matrix bone1 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.x);
    matrix bone2 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.y);
    matrix bone3 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.z);
    matrix bone4 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.w);
    
    matrix bindMatrix1 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.x);
    matrix bindMatrix2 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.y);
    matrix bindMatrix3 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.z);
    matrix bindMatrix4 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.w);

    matrix skinMatrix = input.weights.x * mul(bindMatrix1, bone1) +
                        input.weights.y * mul(bindMatrix2, bone2) +
                        input.weights.z * mul(bindMatrix3, bone3) +
                        input.weights.w * mul(bindMatrix4, bone4);
    
    pos = mul(pos, skinMatrix);
    normalMatrixSkinnedIfNecessary = mul(normalMatrixSkinnedIfNecessary, (float3x3)skinMatrix);
#endif // SKINNED
    
    float4 worldPosition = mul(pos, model);
    PSInput result;
    
    #if defined(SHADOW)
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
    
    result.normalWorldSpace = normalize(mul(input.normal, normalMatrixSkinnedIfNecessary));
    
#if defined(NORMAL_MAP) || defined(PARALLAX)
    result.TBN_T = normalize(mul(input.tangent, normalMatrixSkinnedIfNecessary));
    result.TBN_B = normalize(mul(input.bitangent, normalMatrixSkinnedIfNecessary));
    result.TBN_N = normalize(mul(input.normal, normalMatrixSkinnedIfNecessary));
#endif // NORMAL_MAP
    
#if defined(VERTEX_COLORS)
    result.color = input.color;
#endif
#if defined(TEXTURED)
    result.texcoord = input.texcoord;
#endif
    return result;
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
    
    Meshlet meshlet = meshletBuffer[vGroupID.x];
    SetMeshOutputCounts(meshlet.VertCount, meshlet.TriCount);
    
    uint triOffset = meshlet.TriOffset * 3 + uGroupThreadID * 3;
    uint3 meshletIndices = uint3(asuint(meshletTrianglesBuffer.Load(triOffset)), asuint(meshletTrianglesBuffer.Load(triOffset + 1)), asuint(meshletTrianglesBuffer.Load(triOffset+2))); // Local indices into meshletVerticesBuffer
    uint3 vertexIndices = uint3(meshletVerticesBuffer[meshlet.VertOffset + meshletIndices.x], meshletVerticesBuffer[meshlet.VertOffset + meshletIndices.y], meshletVerticesBuffer[meshlet.VertOffset + meshletIndices.z]); // Global indices into vertexBuffer
    if (uGroupThreadID < meshlet.VertCount) {
        outputVertices[uGroupThreadID] = GetVertexAttributes(vertexBuffer, meshlet.VertOffset+uGroupThreadID);
    }
    if (uGroupThreadID < meshlet.TriCount) {
        outputTriangles[uGroupThreadID] = vertexIndices;
    }
}