#ifndef __SKINNING_COMMON_HLSLI__
#define __SKINNING_COMMON_HLSLI__

#include "cbuffers.hlsli"
#include "structs.hlsli"
#include "vertex.hlsli"

typedef row_major float4x4 SkinningMatrix;

bool IsValidSkinningInstanceSlot(uint skinningInstanceSlot)
{
    return skinningInstanceSlot != 0xFFFFFFFFu;
}

float SkinningMaxAxisScale_RowVector(SkinningMatrix M)
{
    float sx = length(M[0].xyz);
    float sy = length(M[1].xyz);
    float sz = length(M[2].xyz);
    return max(sx, max(sy, sz));
}

float4x4 IdentitySkinMatrix();

float4x4 LoadBoneSkinMatrix(uint skinningInstanceSlot, uint jointIndex)
{
    if (!IsValidSkinningInstanceSlot(skinningInstanceSlot))
    {
        return float4x4(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    StructuredBuffer<SkinningInstanceGPUInfo> skinningInstanceBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::SkinningInstanceInfo)];
    StructuredBuffer<SkinningMatrix> boneTransformsBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::BoneTransforms)];
    StructuredBuffer<SkinningMatrix> inverseBindMatricesBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::InverseBindMatrices)];

    SkinningInstanceGPUInfo skinningInfo = skinningInstanceBuffer[skinningInstanceSlot];
    SkinningMatrix bone = boneTransformsBuffer[skinningInfo.transformOffsetMatrices + jointIndex];
    SkinningMatrix bind = inverseBindMatricesBuffer[skinningInfo.invBindOffsetMatrices + jointIndex];
    SkinningMatrix skin = mul(bone, bind);
    if ((skinningInfo.flags & SkinningInstanceFlag_RowVectorSkinMatrix) != 0u)
    {
        return skin;
    }
    return transpose(skin);
}

float4x4 LoadBoneInverseSkinMatrix(uint skinningInstanceSlot, uint jointIndex)
{
    if (!IsValidSkinningInstanceSlot(skinningInstanceSlot))
    {
        return IdentitySkinMatrix();
    }

    StructuredBuffer<SkinningInstanceGPUInfo> skinningInstanceBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::SkinningInstanceInfo)];
    StructuredBuffer<SkinningMatrix> inverseSkinMatricesBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::InverseSkinMatrices)];

    SkinningInstanceGPUInfo skinningInfo = skinningInstanceBuffer[skinningInstanceSlot];
    SkinningMatrix inverseSkin = inverseSkinMatricesBuffer[skinningInfo.inverseSkinOffsetMatrices + jointIndex];
    if ((skinningInfo.flags & SkinningInstanceFlag_RowVectorSkinMatrix) != 0u)
    {
        return inverseSkin;
    }
    return transpose(inverseSkin);
}

float4x4 IdentitySkinMatrix()
{
    return float4x4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
}

float4x4 BuildSkinMatrix(uint skinningInstanceSlot, SkinningInfluences skinning)
{
    if (!IsValidSkinningInstanceSlot(skinningInstanceSlot))
    {
        return IdentitySkinMatrix();
    }

    return
        skinning.weights0.x * LoadBoneSkinMatrix(skinningInstanceSlot, skinning.joints0.x) +
        skinning.weights0.y * LoadBoneSkinMatrix(skinningInstanceSlot, skinning.joints0.y) +
        skinning.weights0.z * LoadBoneSkinMatrix(skinningInstanceSlot, skinning.joints0.z) +
        skinning.weights0.w * LoadBoneSkinMatrix(skinningInstanceSlot, skinning.joints0.w) +
        skinning.weights1.x * LoadBoneSkinMatrix(skinningInstanceSlot, skinning.joints1.x) +
        skinning.weights1.y * LoadBoneSkinMatrix(skinningInstanceSlot, skinning.joints1.y) +
        skinning.weights1.z * LoadBoneSkinMatrix(skinningInstanceSlot, skinning.joints1.z) +
        skinning.weights1.w * LoadBoneSkinMatrix(skinningInstanceSlot, skinning.joints1.w);
}

#endif // __SKINNING_COMMON_HLSLI__
