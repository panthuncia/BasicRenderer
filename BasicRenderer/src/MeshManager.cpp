#include "MeshManager.h"

#include "ResourceManager.h"
#include "ResourceStates.h"
#include "Mesh.h"
#include "ResourceGroup.h"
#include "BufferView.h"

MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_vertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"vertices", true);
	m_meshletOffsets = resourceManager.CreateIndexedDynamicBuffer(sizeof(meshopt_Meshlet), 1, ResourceState::ALL_SRV, L"meshletOffsets");
	m_meshletIndices = resourceManager.CreateIndexedDynamicBuffer(sizeof(unsigned int), 1, ResourceState::ALL_SRV, L"meshletIndices");
	m_meshletTriangles = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"meshletTriangles", true);
	m_resourceGroup = std::make_shared<ResourceGroup>(L"MeshInfo");
	m_resourceGroup->AddResource(m_vertices);
	m_resourceGroup->AddResource(m_meshletOffsets);
	m_resourceGroup->AddResource(m_meshletIndices);
	m_resourceGroup->AddResource(m_meshletTriangles);

	m_opaquePerMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"OpaquePerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
	m_transparentPerMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"TransparentPerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
}

void MeshManager::AddMesh(std::shared_ptr<Mesh>& mesh, MaterialBuckets bucket) {
	auto& vertices = mesh->GetVertices();
    if (vertices.empty()) {
        // Handle empty vertices case
		throw std::runtime_error("Mesh vertices are empty");
    }

	auto& manager = ResourceManager::GetInstance();
	std::unique_ptr<BufferView> view = nullptr;
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
		view = m_vertices->Allocate(size, typeid(VertexType));

        // Map and copy data
        VertexType* dataPtr = view->Map<VertexType>();
        std::memcpy(dataPtr, specificVertices.data(), size);
		view->GetBuffer()->MarkViewDirty(view.get());
		//mesh->SetVertexBufferView(std::move(view));
		manager.QueueViewedDynamicBufferViewUpdate(m_vertices.get());

        }, vertices.front());

	int size = sizeof(VertexTextured);
	auto& meshlets = mesh->GetMeshlets();
	auto test = vertices[0];
	spdlog::info("Adding {} meshlets, allocating {} bytes", meshlets.size(), meshlets.size() * sizeof(meshopt_Meshlet));
	auto meshletOffsetsView = m_meshletOffsets->Allocate(meshlets.size() * sizeof(meshopt_Meshlet), typeid(meshopt_Meshlet));
	std::memcpy(meshletOffsetsView->Map<meshopt_Meshlet>(), meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet));
	meshletOffsetsView->GetBuffer()->MarkViewDirty(meshletOffsetsView.get());
	//mesh->SetMeshletOffsetsBufferView(std::move(meshletOffsetsView));
	manager.QueueViewedDynamicBufferViewUpdate(m_meshletOffsets.get());

	auto& meshletVertices = mesh->GetMeshletVertices();
	auto meshletIndicesView = m_meshletIndices->Allocate(meshletVertices.size() * sizeof(unsigned int), typeid(unsigned int));
	std::memcpy(meshletIndicesView->Map<unsigned int>(), meshletVertices.data(), meshletVertices.size() * sizeof(unsigned int));
	meshletIndicesView->GetBuffer()->MarkViewDirty(meshletIndicesView.get());
	//mesh->SetMeshletVerticesBufferView(std::move(meshletIndicesView));
	manager.QueueViewedDynamicBufferViewUpdate(m_meshletIndices.get());

	auto& meshletTriangles = mesh->GetMeshletTriangles();
	auto meshletTrianglesView = m_meshletTriangles->Allocate(meshletTriangles.size() * sizeof(unsigned char), typeid(unsigned char));
	std::memcpy(meshletTrianglesView->Map<unsigned char>(), meshletTriangles.data(), meshletTriangles.size() * sizeof(unsigned char));
	meshletTrianglesView->GetBuffer()->MarkViewDirty(meshletTrianglesView.get());
	//mesh->SetMeshletTrianglesBufferView(std::move(meshletTrianglesView));
	manager.QueueViewedDynamicBufferViewUpdate(m_meshletTriangles.get());

	mesh->SetBufferViews(std::move(view), std::move(meshletOffsetsView), std::move(meshletIndicesView), std::move(meshletTrianglesView));

	// Per mesh buffer
	switch (bucket){
	case MaterialBuckets::Opaque: {
		auto perMeshBufferView = m_opaquePerMeshBuffers->Allocate(sizeof(PerMeshCB), typeid(PerMeshCB));
		std::memcpy(perMeshBufferView->Map<PerMeshCB>(), &mesh->GetPerMeshCBData(), sizeof(PerMeshCB));
		perMeshBufferView->GetBuffer()->MarkViewDirty(perMeshBufferView.get());
		mesh->SetPerMeshBufferView(std::move(perMeshBufferView));
		manager.QueueViewedDynamicBufferViewUpdate(m_opaquePerMeshBuffers.get());
		break;
	}
	case MaterialBuckets::Transparent: {
		auto perMeshBufferView = m_transparentPerMeshBuffers->Allocate(sizeof(PerMeshCB), typeid(PerMeshCB));
		std::memcpy(perMeshBufferView->Map<PerMeshCB>(), &mesh->GetPerMeshCBData(), sizeof(PerMeshCB));
		perMeshBufferView->GetBuffer()->MarkViewDirty(perMeshBufferView.get());
		mesh->SetPerMeshBufferView(std::move(perMeshBufferView));
		manager.QueueViewedDynamicBufferViewUpdate(m_transparentPerMeshBuffers.get());
		break;
	}
	}
	
}

// TODO: finish
void MeshManager::RemoveMesh(std::shared_ptr<BufferView> view) {
	m_vertices->Deallocate(view);
}

void MeshManager::UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data) {
	std::memcpy(view->Map<PerMeshCB>(), &data, sizeof(PerMeshCB));
	view->GetBuffer()->MarkViewDirty(view.get());
	ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(view->GetBuffer());
}