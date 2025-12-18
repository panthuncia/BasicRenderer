#include "Animation/Skeleton.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <flecs.h>
#include <unordered_set>

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Scene/Components.h"
#include "Animation/AnimationController.h"

Skeleton::Skeleton(const std::vector<flecs::entity>& nodes, const std::vector<XMMATRIX>& inverseBindMatrices)
    : m_bones(nodes), m_inverseBindMatrices(inverseBindMatrices) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX));
    m_transformsBuffer->SetName(L"BoneTransforms");
    m_inverseBindMatricesBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX));
	m_inverseBindMatricesBuffer->SetName(L"InverseBindMatrices");
    QUEUE_UPLOAD(m_inverseBindMatrices.data(), nodes.size() * sizeof(XMMATRIX), m_inverseBindMatricesBuffer, 0);
	m_isBaseSkeleton = true;
    FindRoot();
}

Skeleton::Skeleton(const std::vector<flecs::entity>& nodes, std::shared_ptr<Buffer> inverseBindMatrices)
    : m_bones(nodes), m_inverseBindMatricesBuffer(inverseBindMatrices) {
    m_boneTransforms.resize(nodes.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(nodes.size(), sizeof(DirectX::XMMATRIX));
    m_transformsBuffer->SetName(L"BoneTransforms");
    FindRoot();
}

Skeleton::Skeleton(const Skeleton& other) {

    std::unordered_map<uint64_t, uint64_t> oldBonesToNewBonesIDMap;
	std::unordered_map<uint64_t, flecs::entity> oldBoneIDToNewBonesMap;
	auto& ecs_world = ECSManager::GetInstance().GetWorld();
    // Clone each bone entity.
    for (auto& oldBone : other.m_bones) {
        flecs::entity newBone = ecs_world.entity();

        // Copy components from oldBone to newBone.
		newBone.set<Components::Rotation>({ oldBone.get<Components::Rotation>().rot });
		newBone.set<Components::Position>({ oldBone.get<Components::Position>().pos });
		newBone.set<Components::Scale>({ oldBone.get<Components::Scale>().scale });
		newBone.set<Components::Matrix>(DirectX::XMMatrixIdentity());
		auto animationController = oldBone.get<AnimationController>();
        newBone.set<AnimationController>(animationController);
		newBone.set<Components::AnimationName>({ oldBone.get<Components::AnimationName>().name });

        // Save in mapping.
        oldBonesToNewBonesIDMap[oldBone.id()] = newBone.id();
        oldBoneIDToNewBonesMap[oldBone.id()] = newBone;
        m_bones.push_back(newBone);
    }

    // Rebuild the hierarchy
    // For each old bone, if it had a parent, add the new bone as a child of the new parent
    for (auto& oldBone : other.m_bones) {
        flecs::entity& newBone = oldBoneIDToNewBonesMap[oldBone.id()];

        flecs::entity oldParent = oldBone.parent();
        if (oldParent.is_valid() && oldBoneIDToNewBonesMap.contains(oldParent.id())) {
            // Add the new bone as a child of the new parent.
			auto& newParent = oldBoneIDToNewBonesMap[oldParent.id()];
            if (newParent.is_valid()) {
                newBone.child_of(newParent);
            }
        }
        else {
            // This bone is a root
            m_root = newBone;
            m_root.add<Components::SkeletonRoot>();
        }
    }

    // Clone animations.
    for (auto& anim : other.animations) {
        auto newAnim = std::make_shared<Animation>(anim->name);
        for (auto& pair : anim->nodesMap) {
            newAnim->nodesMap[pair.first] = pair.second;
        }
        AddAnimation(newAnim);
    }

	// copy inverse bind matrices
	m_inverseBindMatrices = other.m_inverseBindMatrices;
    m_boneTransforms.resize(m_bones.size() * 16);
    auto& resourceManager = ResourceManager::GetInstance();
    m_transformsBuffer = resourceManager.CreateIndexedStructuredBuffer(m_bones.size(), sizeof(DirectX::XMMATRIX));
    m_transformsBuffer->SetName(L"BoneTransforms");
}

void Skeleton::FindRoot() {
    std::unordered_set<uint64_t> boneIDs;
    for (auto& bone : m_bones) {
		boneIDs.insert(bone.id());
    }
	for (auto& bone : m_bones) {
		flecs::entity parent = bone.parent();
		if (parent.is_valid() && boneIDs.contains(parent.id())) {
			// This bone has a parent that is also in the list of bones
			continue;
		}
		else {
			// This bone is a root
			m_root = bone;
			m_root.add<Components::SkeletonRoot>();
			return;
		}
	}
}

std::shared_ptr<Skeleton> Skeleton::CopySkeleton(bool retainIsBaseSkeleton) {
	auto skeletonCopy = std::make_shared<Skeleton>(*this);
	skeletonCopy->m_isBaseSkeleton = retainIsBaseSkeleton ? m_isBaseSkeleton : false;
	return skeletonCopy;
}

Skeleton::~Skeleton() {
	auto& deletionManager = DeletionManager::GetInstance();
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
		auto name = node.get<Components::AnimationName>();
        if (animation->nodesMap.find(name.name.c_str()) != animation->nodesMap.end()) {
            AnimationController* controller = node.try_get_mut<AnimationController>();
#ifdef _DEBUG
			if (!controller) {
				spdlog::warn("Skeleton node {} does not have an AnimationController component", node.name().c_str());
			}
#endif
            controller->setAnimationClip(animation->nodesMap[name.name.c_str()]);
        }
    }
}

void Skeleton::UpdateTransforms() {
    for (size_t i = 0; i < m_bones.size(); ++i) {
		const Components::Matrix transform = m_bones[i].get<Components::Matrix>();
        memcpy(&m_boneTransforms[i * 16], &transform.matrix, sizeof(XMMATRIX));
    }
    QUEUE_UPLOAD(m_boneTransforms.data(), m_bones.size() * sizeof(XMMATRIX), m_transformsBuffer, 0);
}

uint32_t Skeleton::GetTransformsBufferIndex() {
    return m_transformsBuffer->GetSRVInfo(0).slot.index;
}

uint32_t Skeleton::GetInverseBindMatricesBufferIndex() {
    return m_inverseBindMatricesBuffer->GetSRVInfo(0).slot.index;
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
		AnimationController* controller = node.try_get_mut<AnimationController>();
#ifdef _DEBUG
		if (!controller) {
			spdlog::warn("Skeleton node {} does not have an AnimationController component", node.name().c_str());
		}
#endif
		controller->SetAnimationSpeed(speed);
	}
}