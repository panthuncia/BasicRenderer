#include "Mesh/MeshInstance.h"
#include "Managers/MeshManager.h"
#include "Managers/SkeletonManager.h"

MeshInstance::~MeshInstance() {
    ReleaseSkinningInstance_();
}

void MeshInstance::ReleaseSkinningInstance_() {
    if (m_pCurrentSkeletonManager == nullptr || m_skeleton == nullptr) {
        return;
    }

    auto lifetime = m_skeletonManagerLifetime.lock();
    if (!lifetime || !lifetime->load(std::memory_order_acquire)) {
        m_pCurrentSkeletonManager = nullptr;
        m_skeletonManagerLifetime.reset();
        return;
    }

    if (m_skeleton->GetSkinningInstanceSlot() == 0xFFFFFFFFu) {
        return;
    }

    m_pCurrentSkeletonManager->ReleaseSkinningInstance(m_skeleton.get());
}

void MeshInstance::SetCurrentSkeletonManager(SkeletonManager* manager) {
    m_pCurrentSkeletonManager = manager;
    if (manager != nullptr) {
        m_skeletonManagerLifetime = manager->GetLifetimeToken();
    }
    else {
        m_skeletonManagerLifetime.reset();
    }
}

void MeshInstance::SyncSkinningStateFromSkeleton() {
    if (m_skeleton != nullptr) {
        m_perMeshInstanceBufferData.skinningInstanceSlot = m_skeleton->GetSkinningInstanceSlot();
        m_perMeshInstanceBufferData.skinnedBoundsScale = m_skeleton->GetCurrentAnimationConservativeBoundsScale();
    }
    else {
        m_perMeshInstanceBufferData.skinningInstanceSlot = 0xFFFFFFFF;
        m_perMeshInstanceBufferData.skinnedBoundsScale = 1.0f;
    }

    if (m_pCurrentMeshManager != nullptr && m_perMeshInstanceBufferView != nullptr) {
        m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
    }
}

void MeshInstance::SetBufferViews(std::unique_ptr<BufferView> perMeshInstanceBufferView) {
	m_perMeshInstanceBufferView = std::move(perMeshInstanceBufferView);

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}

void MeshInstance::SetBufferViewUsingBaseMesh(std::unique_ptr<BufferView> perMeshInstanceBufferView) {
	m_perMeshInstanceBufferView = std::move(perMeshInstanceBufferView);
	//Skinning
	auto postSkinningView = m_mesh->GetPostSkinningVertexBufferView();
	if (m_perMeshInstanceBufferView == nullptr) {
		return; // no need to update
	}

	m_perMeshInstanceBufferData.postSkinningVertexBufferOffset = (postSkinningView != nullptr)
		? static_cast<uint32_t>(postSkinningView->GetOffset())
		: 0u;
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}

void MeshInstance::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
    if (m_skeleton == skeleton) {
        SyncSkinningStateFromSkeleton();
        return;
    }

    ReleaseSkinningInstance_();
	m_skeleton = skeleton;
    SyncSkinningStateFromSkeleton();
}

void MeshInstance::SetPerObjectBufferIndex(uint32_t index) {
	m_perMeshInstanceBufferData.perObjectBufferIndex = index;
	if (m_pCurrentMeshManager && m_perMeshInstanceBufferView) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}
void MeshInstance::SetPerMeshBufferIndex(uint32_t index) {
	m_perMeshInstanceBufferData.perMeshBufferIndex = index;
	if (m_pCurrentMeshManager && m_perMeshInstanceBufferView) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}

void MeshInstance::SetSkinningInstanceSlot(uint32_t slot) {
	m_perMeshInstanceBufferData.skinningInstanceSlot = slot;
    if (m_skeleton != nullptr) {
        m_skeleton->SetSkinningInstanceSlot(slot);
    }
	if (m_pCurrentMeshManager && m_perMeshInstanceBufferView) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}
