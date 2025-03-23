#include "MeshManager.h"

#include "ResourceManager.h"
#include "ResourceStates.h"
#include "Mesh.h"
#include "ResourceGroup.h"
#include "BufferView.h"
#include "MeshInstance.h"

MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_preSkinningVertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"preSkinnedVertices", true);
	m_postSkinningVertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"PostSkinnedvertices", true, true);
	m_meshletOffsets = resourceManager.CreateIndexedDynamicBuffer(sizeof(meshopt_Meshlet), 1, ResourceState::ALL_SRV, L"meshletOffsets");
	m_meshletIndices = resourceManager.CreateIndexedDynamicBuffer(sizeof(unsigned int), 1, ResourceState::ALL_SRV, L"meshletIndices");
	m_meshletTriangles = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"meshletTriangles", true);
	m_resourceGroup = std::make_shared<ResourceGroup>(L"MeshInfo");
	m_resourceGroup->AddResource(m_meshletOffsets);
	m_resourceGroup->AddResource(m_meshletIndices);
	m_resourceGroup->AddResource(m_meshletTriangles);

	m_opaquePerMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"OpaquePerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
	m_alphaTestPerMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"AlphaTestPerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
	m_blendPerMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"BlendPerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
	
	m_perMeshInstanceBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"perMeshInstanceBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
}

void MeshManager::AddMesh(std::shared_ptr<Mesh>& mesh, MaterialBuckets bucket) {
	auto& vertices = mesh->GetVertices();
	auto& skinningVertices = mesh->GetSkinningVertices();
    if (vertices.empty()) {
        // Handle empty vertices case
		throw std::runtime_error("Mesh vertices are empty");
    }

	auto& manager = ResourceManager::GetInstance();
	std::unique_ptr<BufferView> postSkinningView = nullptr;
	std::unique_ptr<BufferView> preSkinningView = nullptr;

	size_t vertexByteSize = mesh->GetPerMeshCBData().vertexByteSize;
	if (mesh->GetPerMeshCBData().vertexFlags & VertexFlags::VERTEX_SKINNED) {
		unsigned int skinningVertexByteSize = mesh->GetSkinningVertexSize();
		preSkinningView = m_preSkinningVertices->AddData(skinningVertices.data(), mesh->GetNumVertices() * skinningVertexByteSize, skinningVertexByteSize);
		//postSkinningView = m_postSkinningVertices->AddData(vertices.data(), mesh->GetNumVertices() * vertexByteSize, vertexByteSize);
	}
	else {
		postSkinningView = m_postSkinningVertices->AddData(vertices.data(), mesh->GetNumVertices() * vertexByteSize, vertexByteSize);
	}

	auto& meshlets = mesh->GetMeshlets();
	//auto test = vertices[0];
	spdlog::info("Adding {} meshlets, allocating {} bytes", meshlets.size(), meshlets.size() * sizeof(meshopt_Meshlet));
	auto meshletOffsetsView = m_meshletOffsets->AddData(meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet), sizeof(meshopt_Meshlet));

	auto& meshletVertices = mesh->GetMeshletVertices();
	auto meshletIndicesView = m_meshletIndices->AddData(meshletVertices.data(), meshletVertices.size() * sizeof(unsigned int), sizeof(unsigned int));

	auto& meshletTriangles = mesh->GetMeshletTriangles();
	auto meshletTrianglesView = m_meshletTriangles->AddData(meshletTriangles.data(), meshletTriangles.size() * sizeof(unsigned char), sizeof(unsigned char));

	mesh->SetBufferViews(std::move(preSkinningView), std::move(postSkinningView), std::move(meshletOffsetsView), std::move(meshletIndicesView), std::move(meshletTrianglesView));

	// Per mesh buffer
	switch (bucket){
	case MaterialBuckets::Opaque: {
		auto perMeshBufferView = m_opaquePerMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), sizeof(PerMeshCB));
		mesh->SetPerMeshBufferView(std::move(perMeshBufferView));
		break;
	}
	case MaterialBuckets::AlphaTest: {
		auto perMeshBufferView = m_alphaTestPerMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), sizeof(PerMeshCB));
		mesh->SetPerMeshBufferView(std::move(perMeshBufferView));
		break;
	}
	case MaterialBuckets::Blend: {
		auto perMeshBufferView = m_blendPerMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), sizeof(PerMeshCB));
		mesh->SetPerMeshBufferView(std::move(perMeshBufferView));
		break;
	}

	}
	
}

// TODO: finish
void MeshManager::RemoveMesh(Mesh* mesh) {
	auto preSkinningView = mesh->GetPreSkinningVertexBufferView();
	if (preSkinningView != nullptr) {
		m_preSkinningVertices->Deallocate(preSkinningView);
	}
}

void MeshManager::AddMeshInstance(MeshInstance* mesh) {
	mesh->SetCurrentMeshManager(this);
	auto& vertices = mesh->GetMesh()->GetVertices();

	auto postSkinningView = m_postSkinningVertices->AddData(vertices.data(), vertices.size(), mesh->GetMesh()->GetPerMeshCBData().vertexByteSize);
	auto perMeshInstanceBufferView = m_perMeshInstanceBuffers->AddData(&mesh->GetPerMeshInstanceBufferData(), sizeof(PerMeshInstanceCB), sizeof(PerMeshInstanceCB));
	mesh->SetBufferViews(std::move(postSkinningView), std::move(perMeshInstanceBufferView));
}

void MeshManager::RemoveMeshInstance(MeshInstance* mesh) {
	m_postSkinningVertices->Deallocate(mesh->GetPostSkinningVertexBufferView());
	mesh->SetBufferViews(nullptr, nullptr);
}

void MeshManager::UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data) {
	view->GetBuffer()->UpdateView(view.get(), &data);
}

void MeshManager::UpdatePerMeshInstanceBuffer(std::unique_ptr<BufferView>& view, PerMeshInstanceCB& data) {
	view->GetBuffer()->UpdateView(view.get(), &data);
}