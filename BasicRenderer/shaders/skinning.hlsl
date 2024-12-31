#include "vertex.hlsli"
#include "structs.hlsli"
#include "cbuffers.hlsli"

[numthreads(64, 1, 1)]
void CSMain(uint dispatchID : SV_DispatchThreadID) {
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    
    if (dispatchID >= meshBuffer.numVertices) {
        return;
    }
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[perObjectBufferDescriptorIndex];
    PerObjectBuffer objectBuffer = perObjectBuffer[perObjectBufferIndex];
    
    ByteAddressBuffer preSkinningVertexBuffer = ResourceDescriptorHeap[preSkinningVertexBufferDescriptorIndex];
    RWByteAddressBuffer postSkinningVertexBuffer = ResourceDescriptorHeap[postSkinningVertexBufferDescriptorIndex];
    
    StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[normalMatrixBufferDescriptorIndex];
    
    uint preSkinnedByteOffset = meshBuffer.preSkinningVertexBufferOffset + dispatchID * meshBuffer.skinningVertexByteSize;
    uint postSkinnedByteOffset = meshBuffer.postSkinningVertexBufferOffset + dispatchID * meshBuffer.vertexByteSize;
    
    Vertex input = LoadSkinningVertex(preSkinnedByteOffset, preSkinningVertexBuffer, meshBuffer.vertexFlags);
    
    float4 pos = float4(input.position.xyz, 1.0f);
    
    StructuredBuffer<float4x4> boneTransformsBuffer = ResourceDescriptorHeap[objectBuffer.boneTransformBufferIndex];
    StructuredBuffer<float4x4> inverseBindMatricesBuffer = ResourceDescriptorHeap[objectBuffer.inverseBindMatricesBufferIndex];
    
    matrix bone1 = (boneTransformsBuffer[input.joints.x]);
    matrix bone2 = (boneTransformsBuffer[input.joints.y]);
    matrix bone3 = (boneTransformsBuffer[input.joints.z]);
    matrix bone4 = (boneTransformsBuffer[input.joints.w]);
    
    matrix bindMatrix1 = (inverseBindMatricesBuffer[input.joints.x]);
    matrix bindMatrix2 = (inverseBindMatricesBuffer[input.joints.y]);
    matrix bindMatrix3 = (inverseBindMatricesBuffer[input.joints.z]);
    matrix bindMatrix4 = (inverseBindMatricesBuffer[input.joints.w]);
    
    float4x4 skinMatrix = transpose(input.weights.x * mul(bone1, bindMatrix1) +
                         input.weights.y * mul(bone2, bindMatrix2) +
                         input.weights.z * mul(bone3, bindMatrix3) +
                         input.weights.w * mul(bone4, bindMatrix4));
    
    float3x3 normalMatrix = (float3x3)mul(normalMatrixBuffer[objectBuffer.normalMatrixBufferIndex], skinMatrix);
    
    float3 skinnedPosition = mul(pos, skinMatrix).xyz;
    float3 skinnedNormal = mul(input.normal, normalMatrix);
    postSkinningVertexBuffer.Store3(postSkinnedByteOffset, asuint(skinnedPosition));
    uint byteOffset = 12;
    postSkinningVertexBuffer.Store3(postSkinnedByteOffset + byteOffset, asuint(skinnedNormal));
    byteOffset += 12 + 8; // float3 normal, float2 texcoord
    if (meshBuffer.vertexFlags & VERTEX_TANBIT)
    {
        float3 skinnedTangent = mul(input.tangent, normalMatrix);
        float3 skinnedBitangent = mul(input.bitangent, normalMatrix);
        postSkinningVertexBuffer.Store3(postSkinnedByteOffset + byteOffset, asuint(skinnedTangent));
        byteOffset += 12;
        postSkinningVertexBuffer.Store3(postSkinnedByteOffset + byteOffset, asuint(skinnedBitangent));

    }
}