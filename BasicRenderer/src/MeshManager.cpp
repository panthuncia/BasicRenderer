#include "MeshManager.h"

#include "ResourceManager.h"
#include "ResourceStates.h"
#include "Mesh.h"
MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_vertices = resourceManager.CreateIndexedDynamicBuffer(1, ResourceState::ALL_SRV, L"vertices");
	m_meshletOffsets = resourceManager.CreateIndexedDynamicBuffer(1, ResourceState::ALL_SRV, L"meshletOffsets");
	m_meshletIndices = resourceManager.CreateIndexedDynamicBuffer(1, ResourceState::ALL_SRV, L"meshletIndices");
	m_meshletTriangles = resourceManager.CreateIndexedDynamicBuffer(1, ResourceState::ALL_SRV, L"meshletTriangles");

}

void MeshManager::AddMesh(std::shared_ptr<Mesh>& mesh) {
	auto& vertices = mesh->GetVertices();
    if (vertices.empty()) {
        // Handle empty vertices case
		throw std::runtime_error("Mesh vertices are empty");
    }

    // Use std::visit to determine the concrete vertex type
    std::visit([&](auto&& vertexSample) {
        using VertexType = std::decay_t<decltype(vertexSample)>;

        // Allocate buffer view
        size_t size = vertices.size() * sizeof(VertexType);
		std::unique_ptr<BufferView> view = m_vertices.buffer->Allocate(size, typeid(VertexType));

        // Map and copy data
        VertexType* dataPtr = view->Map<VertexType>();
        std::memcpy(dataPtr, vertices.data(), size);
        view->Unmap();

		mesh->SetVertexBufferView(std::move(view));

        }, vertices.front());
    
	auto& meshlets = mesh->GetMeshlets();
	auto meshletOffsetsView = m_meshletOffsets.buffer->Allocate(meshlets.size() * sizeof(meshopt_Meshlet), typeid(meshopt_Meshlet));
	std::memcpy(meshletOffsetsView->Map<meshopt_Meshlet>(), meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet));
	mesh->SetMeshletOffsetsBufferView(std::move(meshletOffsetsView));

	auto& meshletVertices = mesh->GetMeshletVertices();
	auto meshletIndicesView = m_meshletIndices.buffer->Allocate(meshletVertices.size() * sizeof(unsigned int), typeid(unsigned int));
	std::memcpy(meshletIndicesView->Map<unsigned int>(), meshletVertices.data(), meshletVertices.size() * sizeof(unsigned int));
	mesh->SetMeshletVerticesBufferView(std::move(meshletIndicesView));

	auto& meshletTriangles = mesh->GetMeshletTriangles();
	auto meshletTrianglesView = m_meshletTriangles.buffer->Allocate(meshletTriangles.size() * sizeof(unsigned char), typeid(unsigned char));
	std::memcpy(meshletTrianglesView->Map<unsigned char>(), meshletTriangles.data(), meshletTriangles.size() * sizeof(unsigned char));
	mesh->SetMeshletTrianglesBufferView(std::move(meshletTrianglesView));
}

void MeshManager::RemoveMesh(std::shared_ptr<BufferView> view) {
	m_vertices.buffer->Deallocate(view);
}
