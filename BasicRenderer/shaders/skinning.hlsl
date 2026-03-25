#include "include/vertex.hlsli"
#include "include/structs.hlsli"
#include "include/cbuffers.hlsli"
#include "include/skinningCommon.hlsli"

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    uint vertexOffsetInMesh = dtid.x;
    if (vertexOffsetInMesh >= meshBuffer.numVertices) {
        return;
    }
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    PerObjectBuffer objectBuffer = perObjectBuffer[perObjectBufferIndex];
    
    ByteAddressBuffer preSkinningVertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PreSkinningVertices)];
    RWByteAddressBuffer postSkinningVertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    
    StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::NormalMatrixBuffer)];
    
    uint preSkinnedByteOffset = meshBuffer.vertexBufferOffset + vertexOffsetInMesh * meshBuffer.skinningVertexByteSize;
    uint postSkinnedByteOffset = meshInstanceBuffer.postSkinningVertexBufferOffset;
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    postSkinnedByteOffset += meshBuffer.vertexByteSize * meshBuffer.numVertices * (perFrameBuffer.frameIndex % 2); // ping-pong for motion vectors
    postSkinnedByteOffset += vertexOffsetInMesh * meshBuffer.vertexByteSize;
    
    Vertex input = LoadSkinningVertex(preSkinnedByteOffset, preSkinningVertexBuffer, meshBuffer.vertexFlags);
    
    float4 pos = float4(input.position.xyz, 1.0f);
    
    float4x4 skinMatrix = BuildSkinMatrix(meshInstanceBuffer.skinningInstanceSlot, input.skinning);
    
    float3x3 normalMatrix = (float3x3)mul(normalMatrixBuffer[objectBuffer.normalMatrixBufferIndex], skinMatrix);
    
    float3 skinnedPosition = mul(pos, skinMatrix).xyz;
    float3 skinnedNormal = mul(input.normal, normalMatrix);
    postSkinningVertexBuffer.Store3(postSkinnedByteOffset, asuint(skinnedPosition));
    uint byteOffset = 12;
    postSkinningVertexBuffer.Store3(postSkinnedByteOffset + byteOffset, asuint(skinnedNormal));
    byteOffset += 12 + 8; // float3 normal, float2 texcoord
}
