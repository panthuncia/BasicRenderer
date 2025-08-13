#include "Mesh/MeshInstance.h"
#include "Managers/MeshManager.h"

BufferView* MeshInstance::GetPostSkinningVertexBufferView() {
	return m_postSkinningVertexBufferView.get();
}

void MeshInstance::SetBufferViews(std::unique_ptr<BufferView> postSkinningVertexBufferView, std::unique_ptr<BufferView> perMeshInstanceBufferView,  std::unique_ptr<BufferView> meshletBoundsBufferView) {
	m_postSkinningVertexBufferView = std::move(postSkinningVertexBufferView);
	m_perMeshInstanceBufferView = std::move(perMeshInstanceBufferView);
	m_meshletBoundsBufferView = std::move(meshletBoundsBufferView);
	if (m_postSkinningVertexBufferView == nullptr) {
		return; // no need to update
	}
	m_perMeshInstanceBufferData.postSkinningVertexBufferOffset = static_cast<uint32_t>(m_postSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer is limited to uint32. We need to expand this, ideally with some kind of buffer pool

	m_perMeshInstanceBufferData.meshletBoundsBufferStartIndex = static_cast<uint32_t>(m_meshletBoundsBufferView->GetOffset() / sizeof(BoundingSphere));

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}

void MeshInstance::SetBufferViewUsingBaseMesh(std::unique_ptr<BufferView> perMeshInstanceBufferView) {
	m_perMeshInstanceBufferView = std::move(perMeshInstanceBufferView);
	//Skinning
	auto postSkinningView = m_mesh->GetPostSkinningVertexBufferView();
	if (postSkinningView == nullptr || m_perMeshInstanceBufferView == nullptr) {
		return; // no need to update
	}
	//Meshlet bounds
	auto meshletBoundsView = m_mesh->GetMeshletBoundsBufferView();
	if (meshletBoundsView != nullptr) {
		m_perMeshInstanceBufferData.meshletBoundsBufferStartIndex = static_cast<uint32_t>(meshletBoundsView->GetOffset() / sizeof(BoundingSphere));
	}

	m_perMeshInstanceBufferData.postSkinningVertexBufferOffset = static_cast<uint32_t>(postSkinningView->GetOffset());
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}

void MeshInstance::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
	m_skeleton = skeleton;
	m_perMeshInstanceBufferData.boneTransformBufferIndex = skeleton->GetTransformsBufferIndex();
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
	//skeleton->SetAnimationSpeed(m_animationSpeed);
}

void MeshInstance::SetMeshletBitfieldBufferView(std::unique_ptr<BufferView> meshletBitfieldBufferView) {
	m_meshletBitfieldBufferView = std::move(meshletBitfieldBufferView);
	if (m_meshletBitfieldBufferView == nullptr) {
		return; // no need to update
	}
	m_perMeshInstanceBufferData.meshletBitfieldStartIndex = static_cast<uint32_t>(m_meshletBitfieldBufferView->GetOffset() * 8);
	m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
}