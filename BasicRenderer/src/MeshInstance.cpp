#include "MeshInstance.h"
#include "MeshManager.h"

void MeshInstance::SetPostSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_postSkinningVertexBufferView = std::move(view);
	m_perMeshInstanceBufferData.postSkinningVertexBufferOffset = m_postSkinningVertexBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}

BufferView* MeshInstance::GetPostSkinningVertexBufferView() {
	return m_postSkinningVertexBufferView.get();
}

void MeshInstance::SetBufferViews(std::unique_ptr<BufferView> postSkinningVertexBufferView) {
	m_postSkinningVertexBufferView = std::move(postSkinningVertexBufferView);
	m_perMeshInstanceBufferData.postSkinningVertexBufferOffset = m_postSkinningVertexBufferView->GetOffset();
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshInstanceBuffer(m_perMeshInstanceBufferView, m_perMeshInstanceBufferData);
	}
}

void MeshInstance::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
	m_skeleton = skeleton;
	m_perMeshInstanceBufferData.boneTransformBufferIndex = skeleton->GetTransformsBufferIndex();
}