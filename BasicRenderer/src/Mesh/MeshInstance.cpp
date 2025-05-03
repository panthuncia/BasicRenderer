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
	m_perMeshInstanceBufferData.postSkinningVertexBufferOffset = m_postSkinningVertexBufferView->GetOffset();

	m_perMeshInstanceBufferData.meshletBoundsBufferStartIndex = m_meshletBoundsBufferView->GetOffset() / sizeof(BoundingSphere);

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
		m_perMeshInstanceBufferData.meshletBoundsBufferStartIndex = meshletBoundsView->GetOffset() / sizeof(BoundingSphere);
	}

	m_perMeshInstanceBufferData.postSkinningVertexBufferOffset = postSkinningView->GetOffset();
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