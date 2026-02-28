#include "Mesh/MeshInstance.h"
#include "Managers/MeshManager.h"


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
	m_skeleton = skeleton;
	m_perMeshInstanceBufferData.skinningInstanceSlot = skeleton->GetSkinningInstanceSlot();
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
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
	if (m_pCurrentMeshManager && m_perMeshInstanceBufferView) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}