#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Animation.h"
#include "SceneNode.h"
#include "ResourceManager.h"

class Skeleton {
public:
    std::vector<std::shared_ptr<SceneNode>> m_nodes;
    std::vector<float> m_inverseBindMatrices;
    std::vector<float> m_boneTransforms;
    std::vector<std::shared_ptr<Animation>> animations;
    std::unordered_map<std::string, std::shared_ptr<Animation>> animationsByName;
    std::vector<int> userIDs;

    Skeleton(const std::vector<std::shared_ptr<SceneNode>>& nodes, const std::vector<float>& inverseBindMatrices);
    void AddAnimation(const std::shared_ptr<Animation>& animation);
    void SetAnimation(size_t index);
    void UpdateTransforms();
    UINT GetTransformsBufferIndex();
    UINT GetInverseBindMatricesBufferIndex();

private:
    BufferHandle<XMMATRIX> m_transformsHandle;
    BufferHandle<XMMATRIX> m_inverseBindMatricesHandle;
};