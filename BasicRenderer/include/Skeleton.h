#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <flecs.h>

#include "Animation/Animation.h"

class Buffer;

class Skeleton {
public:
	std::shared_ptr<Skeleton> CopySkeleton(bool retainIsBaseSkeleton = false);
    std::vector<flecs::entity> m_bones;
    std::vector<XMMATRIX> m_inverseBindMatrices;
    std::vector<float> m_boneTransforms;
    std::vector<std::shared_ptr<Animation>> animations;
    std::unordered_map<std::string, std::shared_ptr<Animation>> animationsByName;

    Skeleton(const std::vector<flecs::entity>& nodes, const std::vector<XMMATRIX>& inverseBindMatrices);
    Skeleton(const std::vector<flecs::entity>& nodes, std::shared_ptr<Buffer> inverseBindMatrices); // For copying, since bind matrices never change between instances
    Skeleton(const Skeleton& other);

    ~Skeleton();
    void AddAnimation(const std::shared_ptr<Animation>& animation);
    void SetAnimation(size_t index);
	void SetAnimationSpeed(float speed);
    void UpdateTransforms();
    uint32_t GetTransformsBufferIndex();
    uint32_t GetInverseBindMatricesBufferIndex();
    std::shared_ptr<Buffer>& GetInverseBindMatricesBuffer();

	void DeleteAllAnimations();
	void SetJoints(const std::vector<flecs::entity>& joints);
	flecs::entity GetRoot() { return m_root; }
	bool IsBaseSkeleton() { return m_isBaseSkeleton; }

private:
    std::shared_ptr<Buffer> m_transformsBuffer;
    std::shared_ptr<Buffer> m_inverseBindMatricesBuffer;
	flecs::entity m_root;
	bool m_isBaseSkeleton = false;
    void FindRoot();
};