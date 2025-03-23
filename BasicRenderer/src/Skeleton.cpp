#include "Skeleton.h"
#include <spdlog/spdlog.h>
#include "ResourceManager.h"
#include "DeletionManager.h"
#include "UploadManager.h"

Skeleton::Skeleton(const std::vector<std::shared_ptr<SceneNode>>& nodes, const std::vector<XMMATRIX>& inverseBindMatrices)
    : m_nodes(nodes), m_inverseBindMatrices(inverseBindMatrices) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX), ResourceState::NON_PIXEL_SRV);
    m_transformsBuffer->SetName(L"BoneTransforms");
    m_inverseBindMatricesBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX), ResourceState::NON_PIXEL_SRV);
	m_inverseBindMatricesBuffer->SetName(L"InverseBindMatrices");
    UploadManager::GetInstance().UploadData(m_inverseBindMatrices.data(), nodes.size() * sizeof(XMMATRIX), m_inverseBindMatricesBuffer.get(), 0);
}

Skeleton::Skeleton(const std::vector<std::shared_ptr<SceneNode>>& nodes, std::shared_ptr<Buffer> inverseBindMatrices)
    : m_nodes(nodes), m_inverseBindMatricesBuffer(inverseBindMatrices) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX), ResourceState::NON_PIXEL_SRV);
    m_transformsBuffer->SetName(L"BoneTransforms");
}

Skeleton::Skeleton(const Skeleton& other) {
	// copy nodes
	std::unordered_map<std::shared_ptr<SceneNode>, std::shared_ptr<SceneNode>> nodeMap;
	std::unordered_map<SceneNode*, std::shared_ptr<SceneNode>> nodePtrMap;
	for (auto& node : other.m_nodes) {
		auto newNode = SceneNode::CreateShared(node->m_name);
		newNode->transform = node->transform.copy();
		m_nodes.push_back(newNode);
		nodeMap[node] = newNode;
		nodePtrMap[node.get()] = newNode;
	}
    
	// Rebuild the hierarchy
	for (auto& node : other.m_nodes) {
		if (node->parent) {
            auto parent = nodePtrMap[node->parent];
            if (parent) {
                parent->AddChild(nodeMap[node]);
            }
            else {
				// Probably a root node
            }
		}
	}

    for (auto& animation : other.animations) {
        auto newAnimation = std::make_shared<Animation>(animation->name);
        for (auto& nodePair : animation->nodesMap) {
            SceneNode* node = nodePair.first;
            newAnimation->nodesMap[nodePtrMap[node].get()] = nodePair.second;
        }
        AddAnimation(newAnimation);
    }

	// copy inverse bind matrices
	m_inverseBindMatrices = other.m_inverseBindMatrices;
    m_boneTransforms.resize(m_nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(m_nodes.size(), sizeof(DirectX::XMMATRIX), ResourceState::NON_PIXEL_SRV);
    m_transformsBuffer->SetName(L"BoneTransforms");
}

std::shared_ptr<Skeleton> Skeleton::CopySkeleton() {
	return std::make_shared<Skeleton>(*this);
}

Skeleton::~Skeleton() {
	auto& deletionManager = DeletionManager::GetInstance();
	deletionManager.MarkForDelete(m_transformsBuffer);
	deletionManager.MarkForDelete(m_inverseBindMatricesBuffer);
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
        if (animation->nodesMap.find(node.get()) != animation->nodesMap.end()) {
            node->animationController->setAnimationClip(animation->nodesMap[node.get()]);
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
    UploadManager::GetInstance().UploadData(m_boneTransforms.data(), m_nodes.size() * sizeof(XMMATRIX), m_transformsBuffer.get(), 0);
}

uint32_t Skeleton::GetTransformsBufferIndex() {
    return m_transformsBuffer->GetSRVInfo().index;
}

uint32_t Skeleton::GetInverseBindMatricesBufferIndex() {
    return m_inverseBindMatricesBuffer->GetSRVInfo().index;
}

std::shared_ptr<Buffer>& Skeleton::GetInverseBindMatricesBuffer() {
    return m_inverseBindMatricesBuffer;
}

void Skeleton::DeleteAllAnimations() {
	animations.clear();
	animationsByName.clear();
}

void Skeleton::SetJoints(const std::vector<std::shared_ptr<SceneNode>>& joints) {
	m_nodes = joints;
}