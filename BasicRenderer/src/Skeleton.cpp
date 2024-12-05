#include "Skeleton.h"
#include <spdlog/spdlog.h>
#include "ResourceManager.h"
#include "DeletionManager.h"
#include "UploadManager.h"

Skeleton::Skeleton(const std::vector<std::shared_ptr<SceneNode>>& nodes, const std::vector<XMMATRIX>& inverseBindMatrices)
    : m_nodes(nodes), m_inverseBindMatrices(inverseBindMatrices) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsHandle = resourceManager.CreateIndexedStructuredBuffer<DirectX::XMMATRIX>(nodes.size(), ResourceState::NON_PIXEL_SRV);
    m_transformsHandle.dataBuffer->SetName(L"BoneTransforms");
    m_inverseBindMatricesHandle = resourceManager.CreateIndexedStructuredBuffer<DirectX::XMMATRIX>(nodes.size(), ResourceState::NON_PIXEL_SRV);
	m_inverseBindMatricesHandle.dataBuffer->SetName(L"InverseBindMatrices");
    UploadManager::GetInstance().UploadData(m_inverseBindMatrices.data(), nodes.size() * sizeof(XMMATRIX), m_inverseBindMatricesHandle.dataBuffer.get(), 0);
}

Skeleton::Skeleton(const std::vector<std::shared_ptr<SceneNode>>& nodes, BufferHandle inverseBindMatricesHandle)
    : m_nodes(nodes), m_inverseBindMatricesHandle(inverseBindMatricesHandle) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsHandle = resourceManager.CreateIndexedStructuredBuffer<DirectX::XMMATRIX>(nodes.size(), ResourceState::NON_PIXEL_SRV);
    m_transformsHandle.dataBuffer->SetName(L"BoneTransforms");
}

Skeleton::~Skeleton() {
	auto& deletionManager = DeletionManager::GetInstance();
	deletionManager.MarkForDelete(m_transformsHandle.dataBuffer);
	deletionManager.MarkForDelete(m_inverseBindMatricesHandle.dataBuffer);
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

    auto& animation = animations[index];
    for (auto& node : m_nodes) {
        if (animation->nodesMap.find(node->GetLocalID()) != animation->nodesMap.end()) {
            node->animationController->setAnimationClip(animation->nodesMap[node->GetLocalID()]);
        }
    }
}

void Skeleton::UpdateTransforms() {
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i]->transform.isDirty) {
            spdlog::warn("Skeleton node wasn't updated!");
            m_nodes[i]->Update();
        }

        memcpy(&m_boneTransforms[i * 16], &m_nodes[i]->transform.modelMatrix, sizeof(XMMATRIX));
    }
    UploadManager::GetInstance().UploadData(m_boneTransforms.data(), m_nodes.size() * sizeof(XMMATRIX), m_transformsHandle.dataBuffer.get(), 0);
}

UINT Skeleton::GetTransformsBufferIndex() {
    return m_transformsHandle.dataBuffer->GetSRVInfo().index;
}

UINT Skeleton::GetInverseBindMatricesBufferIndex() {
    return m_inverseBindMatricesHandle.dataBuffer->GetSRVInfo().index;
}

BufferHandle Skeleton::GetInverseBindMatricesHandle() {
    return m_inverseBindMatricesHandle;
}