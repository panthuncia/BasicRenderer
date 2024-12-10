#include "vertex.hlsli"
#include "utilities.hlsli"
#include "cbuffers.hlsli"
#include "structs.hlsli"

PSInput GetVertexAttributes(ByteAddressBuffer buffer, uint blockByteOffset, uint index, uint flags, uint vertexSize, uint3 vGroupID, PerObjectBuffer objectBuffer) {
    uint byteOffset = blockByteOffset + index * vertexSize;
    Vertex vertex = LoadVertex(byteOffset, buffer, flags);
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    float4 pos = float4(vertex.position.xyz, 1.0f);

    float3x3 normalMatrixSkinnedIfNecessary = (float3x3)objectBuffer.normalMatrix;
    
    if (flags & VERTEX_SKINNED) {
        StructuredBuffer<float4x4> boneTransformsBuffer = ResourceDescriptorHeap[objectBuffer.boneTransformBufferIndex];
        StructuredBuffer<float4x4> inverseBindMatricesBuffer = ResourceDescriptorHeap[objectBuffer.inverseBindMatricesBufferIndex];
    
        matrix bone1 = (boneTransformsBuffer[vertex.joints.x]);
        matrix bone2 = (boneTransformsBuffer[vertex.joints.y]);
        matrix bone3 = (boneTransformsBuffer[vertex.joints.z]);
        matrix bone4 = (boneTransformsBuffer[vertex.joints.w]);
        
        matrix bindMatrix1 = (inverseBindMatricesBuffer[vertex.joints.x]);
        matrix bindMatrix2 = (inverseBindMatricesBuffer[vertex.joints.y]);
        matrix bindMatrix3 = (inverseBindMatricesBuffer[vertex.joints.z]);
        matrix bindMatrix4 = (inverseBindMatricesBuffer[vertex.joints.w]);
        
        float4x4 skinMatrix = transpose(vertex.weights.x * mul(bone1, bindMatrix1) +
                             vertex.weights.y * mul(bone2, bindMatrix2) +
                             vertex.weights.z * mul(bone3, bindMatrix3) +
                             vertex.weights.w * mul(bone4, bindMatrix4));
    
        pos = mul(pos, skinMatrix);
        normalMatrixSkinnedIfNecessary = mul(normalMatrixSkinnedIfNecessary, (float3x3) skinMatrix);
    }
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
    
    result.normalWorldSpace = normalize(mul(vertex.normal, normalMatrixSkinnedIfNecessary));
    
    if (flags & VERTEX_TANBIT) {
        result.TBN_T = normalize(mul(vertex.tangent, normalMatrixSkinnedIfNecessary));
        result.TBN_B = normalize(mul(vertex.bitangent, normalMatrixSkinnedIfNecessary));
        result.TBN_N = normalize(mul(vertex.normal, normalMatrixSkinnedIfNecessary));
    }
    
    if (flags & VERTEX_COLORS) {
        result.color = vertex.color;
    };
    
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

    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[vertexBufferDescriptorIndex]; // Base vertex buffer
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[meshletBufferDescriptorIndex]; // Meshlets, containing vertex & primitive offset & num
    StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[meshletVerticesBufferDescriptorIndex]; // Meshlet vertices, as indices into base vertex buffer
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[meshletTrianglesBufferDescriptorIndex]; // meshlet triangles, as local offsets from the current vertex_offset, indexing into meshletVerticesBuffer
    
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[perObjectBufferDescriptorIndex];
    PerObjectBuffer objectBuffer = perObjectBuffer[perObjectBufferIndex];
    
    uint meshletOffset = meshBuffer.meshletBufferOffset;
    Meshlet meshlet = meshletBuffer[meshletOffset+vGroupID.x];
    SetMeshOutputCounts(meshlet.VertCount, meshlet.TriCount);
    
    uint triOffset = meshBuffer.meshletTrianglesBufferOffset + meshlet.TriOffset + uGroupThreadID * 3;
    uint3 meshletIndices = uint3(LoadByte(meshletTrianglesBuffer, triOffset), LoadByte(meshletTrianglesBuffer, triOffset + 1), LoadByte(meshletTrianglesBuffer, triOffset+2)); // Local indices into meshletVerticesBuffer
    uint vertOffset = meshlet.VertOffset + meshBuffer.meshletVerticesBufferOffset;
    
    if (uGroupThreadID < meshlet.VertCount) {
        uint thisVertex = meshletVerticesBuffer[vertOffset + uGroupThreadID];
        outputVertices[uGroupThreadID] = GetVertexAttributes(vertexBuffer, meshBuffer.vertexBufferOffset, thisVertex, meshBuffer.vertexFlags, meshBuffer.vertexByteSize, vGroupID, objectBuffer);
    }
    if (uGroupThreadID < meshlet.TriCount) {
        outputTriangles[uGroupThreadID] = meshletIndices;
    }
}