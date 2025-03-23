#include "MeshInstance.h"
#include "MeshManager.h"

BufferView* MeshInstance::GetPostSkinningVertexBufferView() {
	return m_postSkinningVertexBufferView.get();
}

void MeshInstance::SetBufferViews(std::unique_ptr<BufferView> postSkinningVertexBufferView, std::unique_ptr<BufferView> perMeshInstanceBufferView) {
	m_postSkinningVertexBufferView = std::move(postSkinningVertexBufferView);
	m_perMeshInstanceBufferData.postSkinningVertexBufferOffset = m_postSkinningVertexBufferView->GetOffset();
	m_perMeshInstanceBufferView = std::move(perMeshInstanceBufferView);
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
}