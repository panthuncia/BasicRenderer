#include "Skeleton.h"
#include <spdlog/spdlog.h>

Skeleton::Skeleton(const std::vector<std::shared_ptr<SceneNode>>& nodes, const std::vector<float>& inverseBindMatrices)
    : m_nodes(nodes), m_inverseBindMatrices(inverseBindMatrices) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsHandle = resourceManager.CreateIndexedStructuredBuffer<DirectX::XMMATRIX>(nodes.size());
    m_inverseBindMatricesHandle = resourceManager.CreateIndexedStructuredBuffer<DirectX::XMMATRIX>(nodes.size());
    resourceManager.UpdateStructuredBuffer<DirectX::XMMATRIX>(m_inverseBindMatricesHandle, reinterpret_cast<XMMATRIX*>(m_inverseBindMatrices.data()), 0, nodes.size());
}

void Skeleton::AddAnimation(const std::shared_ptr<Animation>& animation) {
    if (animationsByName.find(animation->name) != animationsByName.end()) {
        spdlog::error("Duplicate animation names are not allowed in a single skeleton");
        return;
    }
    animations.push_back(animation);
    animationsByName[animation->name] = animation;
}

void Skeleton::SetAnimation(size_t index) {
    if (animations.size() <= index) {
        spdlog::error("Animation index out of range");
        return;
    }

    auto animation = animations[index];
    for (auto& node : m_nodes) {
        if (animation->nodesMap.find(node->localID) != animation->nodesMap.end()) {
            node->animationController->setAnimationClip(animation->nodesMap[node->localID]);
        }
    }
}

void Skeleton::UpdateTransforms() {
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i]->transform.isDirty) {
            spdlog::warn("Skeleton node wasn't updated!");
            m_nodes[i]->update();
        }

        // Copy the matrix directly using memcpy
        memcpy(&m_boneTransforms[i * 16], &m_nodes[i]->transform.modelMatrix, sizeof(XMMATRIX));
    }
}