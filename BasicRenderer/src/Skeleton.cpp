#include "Skeleton.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <flecs.h>
#include "ResourceManager.h"
#include "DeletionManager.h"
#include "UploadManager.h"
#include "ECSManager.h"
#include "Components.h"
#include "AnimationController.h"

Skeleton::Skeleton(const std::vector<flecs::entity>& nodes, const std::vector<XMMATRIX>& inverseBindMatrices)
    : m_bones(nodes), m_inverseBindMatrices(inverseBindMatrices) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX), ResourceState::NON_PIXEL_SRV);
    m_transformsBuffer->SetName(L"BoneTransforms");
    m_inverseBindMatricesBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX), ResourceState::NON_PIXEL_SRV);
	m_inverseBindMatricesBuffer->SetName(L"InverseBindMatrices");
    UploadManager::GetInstance().UploadData(m_inverseBindMatrices.data(), nodes.size() * sizeof(XMMATRIX), m_inverseBindMatricesBuffer.get(), 0);
}

Skeleton::Skeleton(const std::vector<flecs::entity>& nodes, std::shared_ptr<Buffer> inverseBindMatrices)
    : m_bones(nodes), m_inverseBindMatricesBuffer(inverseBindMatrices) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX), ResourceState::NON_PIXEL_SRV);
    m_transformsBuffer->SetName(L"BoneTransforms");
}

Skeleton::Skeleton(const Skeleton& other) {

    std::unordered_map<flecs::entity, flecs::entity> boneMap;
	auto& ecs_world = ECSManager::GetInstance().GetWorld();
    // Clone each bone entity.
    for (auto& oldBone : other.m_bones) {
        // Create a new entity in the provided ECS world.
        flecs::entity newBone = ecs_world.entity(oldBone.name().c_str());

        // Copy components (e.g., Transform) from oldBone to newBone.
		newBone.set<Components::Rotation>({ oldBone.get<Components::Rotation>()->rot });
		newBone.set<Components::Position>({ oldBone.get<Components::Position>()->pos });
		newBone.set<Components::Scale>({ oldBone.get<Components::Scale>()->scale });
		newBone.set<Components::Matrix>({ });
        newBone.set<AnimationController>({ *oldBone.get<AnimationController>() });
        // Copy any additional bone-specific components here.

        // Save in mapping.
        boneMap[oldBone] = newBone;
        m_bones.push_back(newBone);
    }

    // Rebuild the hierarchy using flecs relationships.
    // For each old bone, if it had a parent, add the new bone as a child of the new parent.
    for (auto& oldBone : other.m_bones) {
        flecs::entity newBone = boneMap[oldBone];

        flecs::entity oldParent = oldBone.parent();
        if (oldParent.is_valid()) {
            // Add the new bone as a child of the new parent.
            newBone.add(flecs::ChildOf, boneMap[oldParent]);
        }
        else {
            // This bone is a root
            m_root = newBone;
        }
    }

    // Clone animations.
    for (auto& anim : other.animations) {
        auto newAnim = std::make_shared<Animation>(anim->name);
        for (auto& pair : anim->nodesMap) {
            newAnim->nodesMap[pair.first] = pair.second; // Assuming the AnimationClip can be reused or cloned.
        }
        AddAnimation(newAnim);
    }

	// copy inverse bind matrices
	m_inverseBindMatrices = other.m_inverseBindMatrices;
    m_boneTransforms.resize(m_bones.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(m_bones.size(), sizeof(DirectX::XMMATRIX), ResourceState::NON_PIXEL_SRV);
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
    for (auto& node : m_bones) {
        if (animation->nodesMap.find(node.name().c_str()) != animation->nodesMap.end()) {
            AnimationController* controller = node.get_mut<AnimationController>();
#ifdef _DEBUG
			if (!controller) {
				spdlog::warn("Skeleton node {} does not have an AnimationController component", node.name().c_str());
			}
#endif
            controller->setAnimationClip(animation->nodesMap[node.name().c_str()]);
        }
    }
}

void Skeleton::UpdateTransforms() {
    for (size_t i = 0; i < m_bones.size(); ++i) {
		Components::Matrix* transform = m_bones[i].get_mut<Components::Matrix>();
        memcpy(&m_boneTransforms[i * 16], &transform->matrix, sizeof(XMMATRIX));
    }
    UploadManager::GetInstance().UploadData(m_boneTransforms.data(), m_bones.size() * sizeof(XMMATRIX), m_transformsBuffer.get(), 0);
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

void Skeleton::SetJoints(const std::vector<flecs::entity>& joints) {
    m_bones = joints;
}

void Skeleton::SetAnimationSpeed(float speed) {
	for (auto& node : m_bones) {
		AnimationController* controller = node.get_mut<AnimationController>();
#ifdef _DEBUG
		if (!controller) {
			spdlog::warn("Skeleton node {} does not have an AnimationController component", node.name().c_str());
		}
#endif
		controller->SetAnimationSpeed(speed);
	}
}