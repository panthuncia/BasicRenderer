#include "MeshInstance.h"

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