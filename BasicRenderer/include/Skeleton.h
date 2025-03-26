#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Animation.h"
#include "SceneNode.h"

class Buffer;

class Skeleton {
public:
	std::shared_ptr<Skeleton> CopySkeleton();
    std::vector<std::shared_ptr<SceneNode>> m_nodes;
    std::vector<XMMATRIX> m_inverseBindMatrices;
    std::vector<float> m_boneTransforms;
    std::vector<std::shared_ptr<Animation>> animations;
    std::unordered_map<std::string, std::shared_ptr<Animation>> animationsByName;

    Skeleton(const std::vector<std::shared_ptr<SceneNode>>& nodes, const std::vector<XMMATRIX>& inverseBindMatrices);
    Skeleton(const std::vector<std::shared_ptr<SceneNode>>& nodes, std::shared_ptr<Buffer> inverseBindMatrices); // For copying, since bind matrices never change between instances
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
	void SetJoints(const std::vector<std::shared_ptr<SceneNode>>& joints);
	SceneNode* GetRoot() { return m_root; }

private:
    std::shared_ptr<Buffer> m_transformsBuffer;
    std::shared_ptr<Buffer> m_inverseBindMatricesBuffer;
	SceneNode* m_root = nullptr;
};