#include "MeshManager.h"

#include "ResourceManager.h"
#include "ResourceStates.h"
#include "Mesh.h"
MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_vertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"vertices", true);
	m_meshletOffsets = resourceManager.CreateIndexedDynamicBuffer(sizeof(meshopt_Meshlet), 1, ResourceState::ALL_SRV, L"meshletOffsets");
	m_meshletIndices = resourceManager.CreateIndexedDynamicBuffer(sizeof(unsigned int), 1, ResourceState::ALL_SRV, L"meshletIndices");
	m_meshletTriangles = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"meshletTriangles", true);
	m_resourceGroup = std::make_shared<ResourceGroup>(L"MeshInfo");
	m_resourceGroup->AddResource(m_vertices.buffer);
	m_resourceGroup->AddResource(m_meshletOffsets.buffer);
	m_resourceGroup->AddResource(m_meshletIndices.buffer);
	m_resourceGroup->AddResource(m_meshletTriangles.buffer);
}



void MeshManager::AddMesh(std::shared_ptr<Mesh>& mesh) {
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
		view = m_vertices.buffer->Allocate(size, typeid(VertexType));

        // Map and copy data
        VertexType* dataPtr = view->Map<VertexType>();
        std::memcpy(dataPtr, specificVertices.data(), size);
		view->GetBuffer()->MarkViewDirty(view.get());
		//mesh->SetVertexBufferView(std::move(view));
		manager.QueueDynamicBufferViewUpdate(m_vertices.buffer.get());

        }, vertices.front());

	int size = sizeof(VertexTextured);
	auto& meshlets = mesh->GetMeshlets();
	auto test = vertices[0];
	spdlog::info("Adding {} meshlets, allocating {} bytes", meshlets.size(), meshlets.size() * sizeof(meshopt_Meshlet));
	auto meshletOffsetsView = m_meshletOffsets.buffer->Allocate(meshlets.size() * sizeof(meshopt_Meshlet), typeid(meshopt_Meshlet));
	std::memcpy(meshletOffsetsView->Map<meshopt_Meshlet>(), meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet));
	meshletOffsetsView->GetBuffer()->MarkViewDirty(meshletOffsetsView.get());
	//mesh->SetMeshletOffsetsBufferView(std::move(meshletOffsetsView));
	manager.QueueDynamicBufferViewUpdate(m_meshletOffsets.buffer.get());

	auto& meshletVertices = mesh->GetMeshletVertices();
	auto meshletIndicesView = m_meshletIndices.buffer->Allocate(meshletVertices.size() * sizeof(unsigned int), typeid(unsigned int));
	std::memcpy(meshletIndicesView->Map<unsigned int>(), meshletVertices.data(), meshletVertices.size() * sizeof(unsigned int));
	meshletIndicesView->GetBuffer()->MarkViewDirty(meshletIndicesView.get());
	//mesh->SetMeshletVerticesBufferView(std::move(meshletIndicesView));
	manager.QueueDynamicBufferViewUpdate(m_meshletIndices.buffer.get());

	auto& meshletTriangles = mesh->GetMeshletTriangles();
	auto meshletTrianglesView = m_meshletTriangles.buffer->Allocate(meshletTriangles.size() * sizeof(unsigned char), typeid(unsigned char));
	std::memcpy(meshletTrianglesView->Map<unsigned char>(), meshletTriangles.data(), meshletTriangles.size() * sizeof(unsigned char));
	meshletTrianglesView->GetBuffer()->MarkViewDirty(meshletTrianglesView.get());
	//mesh->SetMeshletTrianglesBufferView(std::move(meshletTrianglesView));
	manager.QueueDynamicBufferViewUpdate(m_meshletTriangles.buffer.get());

	mesh->SetBufferViews(std::move(view), std::move(meshletOffsetsView), std::move(meshletIndicesView), std::move(meshletTrianglesView));
}

void MeshManager::RemoveMesh(std::shared_ptr<BufferView> view) {
	m_vertices.buffer->Deallocate(view);
}
