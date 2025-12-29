#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <flecs.h>
#include <vector>
#include <DirectXMath.h>

#include "Animation/Animation.h"


class Skeleton : public std::enable_shared_from_this<Skeleton> {
public:
	std::shared_ptr<Skeleton> CopySkeleton(bool retainIsBaseSkeleton = false);
    std::vector<flecs::entity> m_bones;
    std::vector<DirectX::XMMATRIX> m_inverseBindMatrices;
    std::vector<float> m_boneTransforms;
    std::vector<std::shared_ptr<Animation>> animations;
    std::unordered_map<std::string, std::shared_ptr<Animation>> animationsByName;

    Skeleton(const std::vector<flecs::entity>& nodes, const std::vector<DirectX::XMMATRIX>& inverseBindMatrices);
    Skeleton(const std::vector<flecs::entity>& nodes);
    Skeleton(Skeleton& other);

    ~Skeleton();
    void AddAnimation(const std::shared_ptr<Animation>& animation);
    void SetAnimation(size_t index);
	void SetAnimationSpeed(float speed);

	void DeleteAllAnimations();
	void SetJoints(const std::vector<flecs::entity>& joints);
	flecs::entity GetRoot() { return m_root; }
	bool IsBaseSkeleton() { return m_isBaseSkeleton; }

    std::shared_ptr<Skeleton> GetBaseSkeletonShared() const { return m_baseSkeleton; }
    void SetBaseSkeletonShared(std::shared_ptr<Skeleton> base) { m_baseSkeleton = std::move(base); }

    uint32_t GetSkinningInstanceSlot() const { return m_skinningInstanceSlot; }
    void SetSkinningInstanceSlot(uint32_t slot) { m_skinningInstanceSlot = slot; }

    // Provide a CPU buffer of matrices for uploads (float4x4 or XMMATRIX)
    const void* GetCPUBoneMatrices() const { return m_boneTransforms.data(); }
    void GatherBoneMatricesToCPUBuffer();

private:
	flecs::entity m_root;
	bool m_isBaseSkeleton = false;
    std::shared_ptr<Skeleton> m_baseSkeleton; // null for base skeleton
    uint32_t m_skinningInstanceSlot = 0xFFFFFFFF;
    void FindRoot();
};