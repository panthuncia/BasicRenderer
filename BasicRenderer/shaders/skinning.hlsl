#include "include/vertex.hlsli"
#include "include/structs.hlsli"
#include "include/cbuffers.hlsli"

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
    
    //StructuredBuffer<float4x4> boneTransformsBuffer = ResourceDescriptorHeap[meshInstanceBuffer.boneTransformBufferIndex];
    //StructuredBuffer<float4x4> inverseBindMatricesBuffer = ResourceDescriptorHeap[meshBuffer.inverseBindMatricesBufferIndex];
	StructuredBuffer<SkinningInstanceGPUInfo> skinningInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::SkinningInstanceInfo)];
	SkinningInstanceGPUInfo skinningInfo = skinningInstanceBuffer[meshInstanceBuffer.skinningInstanceSlot];
	StructuredBuffer<float4x4> boneTransformsBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::BoneTransforms)];
    StructuredBuffer<float4x4> inverseBindMatricesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::InverseBindMatrices)];

    matrix bone1 = (boneTransformsBuffer[skinningInfo.transformOffsetMatrices+input.joints.x]);
    matrix bone2 = (boneTransformsBuffer[skinningInfo.transformOffsetMatrices+input.joints.y]);
    matrix bone3 = (boneTransformsBuffer[skinningInfo.transformOffsetMatrices+input.joints.z]);
    matrix bone4 = (boneTransformsBuffer[skinningInfo.transformOffsetMatrices+input.joints.w]);

    matrix bindMatrix1 = (inverseBindMatricesBuffer[skinningInfo.invBindOffsetMatrices+input.joints.x]);
    matrix bindMatrix2 = (inverseBindMatricesBuffer[skinningInfo.invBindOffsetMatrices+input.joints.y]);
    matrix bindMatrix3 = (inverseBindMatricesBuffer[skinningInfo.invBindOffsetMatrices+input.joints.z]);
    matrix bindMatrix4 = (inverseBindMatricesBuffer[skinningInfo.invBindOffsetMatrices+input.joints.w]);

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
}