#ifndef __SKINNING_COMMON_HLSLI__
#define __SKINNING_COMMON_HLSLI__

#include "structs.hlsli"

bool IsValidSkinningInstanceSlot(uint skinningInstanceSlot)
{
    return skinningInstanceSlot != 0xFFFFFFFFu;
}

float SkinningMaxAxisScale_RowVector(float4x4 M)
{
    float sx = length(M[0].xyz);
    float sy = length(M[1].xyz);
    float sz = length(M[2].xyz);
    return max(sx, max(sy, sz));
}

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
    StructuredBuffer<float4x4> boneTransformsBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::BoneTransforms)];
    StructuredBuffer<float4x4> inverseBindMatricesBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::SkeletonResources::InverseBindMatrices)];

    SkinningInstanceGPUInfo skinningInfo = skinningInstanceBuffer[skinningInstanceSlot];
    matrix bone = boneTransformsBuffer[skinningInfo.transformOffsetMatrices + jointIndex];
    matrix bind = inverseBindMatricesBuffer[skinningInfo.invBindOffsetMatrices + jointIndex];
    return transpose(mul(bone, bind));
}

float4x4 BuildSkinMatrix(uint skinningInstanceSlot, uint4 joints, float4 weights)
{
    if (!IsValidSkinningInstanceSlot(skinningInstanceSlot))
    {
        return float4x4(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    return
        weights.x * LoadBoneSkinMatrix(skinningInstanceSlot, joints.x) +
        weights.y * LoadBoneSkinMatrix(skinningInstanceSlot, joints.y) +
        weights.z * LoadBoneSkinMatrix(skinningInstanceSlot, joints.z) +
        weights.w * LoadBoneSkinMatrix(skinningInstanceSlot, joints.w);
}

#endif // __SKINNING_COMMON_HLSLI__
