#include "MeshManager.h"

#include "ResourceManager.h"
#include "ResourceStates.h"
#include "Mesh.h"
#include "ResourceGroup.h"
#include "BufferView.h"

MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_preSkinningVertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"preSkinnedVertices", true);
	m_postSkinningVertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"vertices", true, true);
	m_meshletOffsets = resourceManager.CreateIndexedDynamicBuffer(sizeof(meshopt_Meshlet), 1, ResourceState::ALL_SRV, L"meshletOffsets");
	m_meshletIndices = resourceManager.CreateIndexedDynamicBuffer(sizeof(unsigned int), 1, ResourceState::ALL_SRV, L"meshletIndices");
	m_meshletTriangles = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"meshletTriangles", true);
	m_resourceGroup = std::make_shared<ResourceGroup>(L"MeshInfo");
	m_resourceGroup->AddResource(m_postSkinningVertices);
	m_resourceGroup->AddResource(m_meshletOffsets);
	m_resourceGroup->AddResource(m_meshletIndices);
	m_resourceGroup->AddResource(m_meshletTriangles);

	m_opaquePerMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"OpaquePerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
	m_alphaTestPerMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"AlphaTestPerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
	m_blendPerMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"BlendPerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
}

void MeshManager::AddMesh(std::shared_ptr<Mesh>& mesh, MaterialBuckets bucket) {
	auto& vertices = mesh->GetVertices();
    if (vertices.empty()) {
        // Handle empty vertices case
		throw std::runtime_error("Mesh vertices are empty");
    }

	auto& manager = ResourceManager::GetInstance();
	std::unique_ptr<BufferView> postSkinningView = nullptr;
	std::unique_ptr<BufferView> preSkinningView = nullptr;
    // Use std::visit to determine the concrete vertex type
    std::visit([&](auto&& vertexSample) {
        using VertexType = std::decay_t<decltype(vertexSample)>;
		std::vector<VertexType> specificVertices;
		specificVertices.reserve(vertices.size());
		for (const auto& v : vertices) {
			specificVertices.push_back(std::get<VertexType>(v));
		}
        // Allocate buffer view
        size_t size = vertices.size() * sizeof(VertexType);
		if (mesh->GetPerMeshCBData().vertexFlags & VertexFlags::VERTEX_SKINNED) {
			preSkinningView = m_preSkinningVertices->AddData(specificVertices.data(), size, typeid(VertexType));
			postSkinningView = m_postSkinningVertices->AddData(nullptr, size, typeid(VertexType));
		}
		else {
			postSkinningView = m_postSkinningVertices->AddData(specificVertices.data(), size, typeid(VertexType));
		}

        }, vertices.front());

	int size = sizeof(VertexTextured);
	auto& meshlets = mesh->GetMeshlets();
	auto test = vertices[0];
	spdlog::info("Adding {} meshlets, allocating {} bytes", meshlets.size(), meshlets.size() * sizeof(meshopt_Meshlet));
	auto meshletOffsetsView = m_meshletOffsets->AddData(meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet), typeid(meshopt_Meshlet));

	auto& meshletVertices = mesh->GetMeshletVertices();
	auto meshletIndicesView = m_meshletIndices->AddData(meshletVertices.data(), meshletVertices.size() * sizeof(unsigned int), typeid(unsigned int));

	auto& meshletTriangles = mesh->GetMeshletTriangles();
	auto meshletTrianglesView = m_meshletTriangles->AddData(meshletTriangles.data(), meshletTriangles.size() * sizeof(unsigned char), typeid(unsigned char));

	mesh->SetBufferViews(std::move(postSkinningView), std::move(preSkinningView), std::move(meshletOffsetsView), std::move(meshletIndicesView), std::move(meshletTrianglesView));

	// Per mesh buffer
	switch (bucket){
	case MaterialBuckets::Opaque: {
		auto perMeshBufferView = m_opaquePerMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), typeid(PerMeshCB));
		mesh->SetPerMeshBufferView(std::move(perMeshBufferView));
		break;
	}
	case MaterialBuckets::AlphaTest: {
		auto perMeshBufferView = m_alphaTestPerMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), typeid(PerMeshCB));
		mesh->SetPerMeshBufferView(std::move(perMeshBufferView));
		break;
	}
	case MaterialBuckets::Blend: {
		auto perMeshBufferView = m_blendPerMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), typeid(PerMeshCB));
		mesh->SetPerMeshBufferView(std::move(perMeshBufferView));
		break;
	}

	}
	
}

// TODO: finish
void MeshManager::RemoveMesh(Mesh* mesh) {
	m_postSkinningVertices->Deallocate(mesh->GetPostSkinningVertexBufferView());
	auto preSkinningView = mesh->GetPreSkinningVertexBufferView();
	if (preSkinningView != nullptr) {
		m_preSkinningVertices->Deallocate(preSkinningView);
	}
}

void MeshManager::UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data) {
	view->GetBuffer()->UpdateView(view.get(), &data);
}