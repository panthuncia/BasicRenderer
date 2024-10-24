#include "Mesh.h"

#include <meshoptimizer.h>

#include "DirectX/d3dx12.h"
#include "Utilities.h"
#include "DeviceManager.h"
#include "PSOFlags.h"
#include "ResourceManager.h"
#include "Material.h"
#include "Vertex.h"

std::atomic<int> Mesh::globalMeshCount = 0;

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags) {
    m_vertices = vertices;
    CreateBuffers(vertices, indices);
    this->material = material;
    auto& resourceManager = ResourceManager::GetInstance();
    m_perMeshBufferData.materialDataIndex = material->GetMaterialBufferIndex();
	m_perMeshBufferData.vertexFlags = flags;
	std::visit([&](auto&& vertex) { // Get byte size of vertices
        using T = std::decay_t<decltype(vertex)>;
		m_perMeshBufferData.vertexByteSize = sizeof(T);
		}, vertices.front());
    m_pPerMeshBuffer = resourceManager.CreateConstantBuffer<PerMeshCB>(L"PerMeshCB");
	resourceManager.UpdateConstantBuffer(m_pPerMeshBuffer, m_perMeshBufferData);
	m_globalMeshID = GetNextGlobalIndex();
}

template <typename VertexType>
void Mesh::CreateVertexBuffer(const std::vector<VertexType>& vertices) {
    
    const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(VertexType));

	m_vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, ResourceState::VERTEX, (void*)vertices.data());

    m_vertexBufferView.BufferLocation = m_vertexBufferHandle.dataBuffer->m_buffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(VertexType);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;
}

template <typename VertexType>
void Mesh::CreateMeshlets(const std::vector<VertexType>& vertices, const std::vector<UINT32>& indices) {
	unsigned int maxVertices = 64;
	unsigned int maxPrimitives = 64;
	size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxPrimitives);
    m_meshlets = std::vector<meshopt_Meshlet>(maxMeshlets);
	m_meshletVertices = std::vector<unsigned int>(maxMeshlets*maxVertices);
	m_meshletTriangles = std::vector<unsigned char>(maxMeshlets * maxPrimitives * 3);
    meshopt_buildMeshlets(m_meshlets.data(), m_meshletVertices.data(), m_meshletTriangles.data(), indices.data(), indices.size(), (float*)vertices.data(), vertices.size(), sizeof(VertexType), maxVertices, maxPrimitives, 0);
}

void Mesh::CreateBuffers(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices) {

    std::visit([&](auto&& vertex) {
        using T = std::decay_t<decltype(vertex)>;
        std::vector<T> specificVertices;
        specificVertices.reserve(vertices.size());
        for (const auto& v : vertices) {
            specificVertices.push_back(std::get<T>(v));
        }
		CreateMeshlets(specificVertices, indices);
        CreateVertexBuffer(specificVertices);
        }, vertices.front());

    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(UINT32));
    m_indexCount = indices.size();

	m_indexBufferHandle = ResourceManager::GetInstance().CreateBuffer(indexBufferSize, ResourceState::INDEX, (void*)indices.data());

    m_indexBufferView.BufferLocation = m_indexBufferHandle.dataBuffer->m_buffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;
}

D3D12_VERTEX_BUFFER_VIEW Mesh::GetVertexBufferView() const {
    return m_vertexBufferView;
}

D3D12_INDEX_BUFFER_VIEW Mesh::GetIndexBufferView() const {
    return m_indexBufferView;
}

BufferHandle& Mesh::GetPerMeshBuffer() {
    return m_pPerMeshBuffer;
}

UINT Mesh::GetIndexCount() const {
    return m_indexCount;
}

int Mesh::GetNextGlobalIndex() {
    return globalMeshCount.fetch_add(1, std::memory_order_relaxed);
}

int Mesh::GetGlobalID() const {
	return m_globalMeshID;
}

void Mesh::SetVertexBufferView(std::unique_ptr<BufferView> view) {
	m_vertexBufferView2 = std::move(view);
	m_perMeshBufferData.vertexBufferOffset = m_vertexBufferView2->GetOffset();
	auto& resourceManager = ResourceManager::GetInstance();
	resourceManager.UpdateConstantBuffer(m_pPerMeshBuffer, m_perMeshBufferData);
}
void Mesh::SetMeshletOffsetsBufferView(std::unique_ptr<BufferView> view) {
	m_meshletBufferView = std::move(view);
	m_perMeshBufferData.meshletBufferOffset = m_meshletBufferView->GetOffset();
	auto& resourceManager = ResourceManager::GetInstance();
	resourceManager.UpdateConstantBuffer(m_pPerMeshBuffer, m_perMeshBufferData);
}
void Mesh::SetMeshletVerticesBufferView(std::unique_ptr<BufferView> view) {
	m_meshletVerticesBufferView = std::move(view);
	m_perMeshBufferData.meshletVerticesBufferOffset = m_meshletVerticesBufferView->GetOffset();
	auto& resourceManager = ResourceManager::GetInstance();
	resourceManager.UpdateConstantBuffer(m_pPerMeshBuffer, m_perMeshBufferData);
}
void Mesh::SetMeshletTrianglesBufferView(std::unique_ptr<BufferView> view) {
	m_meshletTrianglesBufferView = std::move(view);
	m_perMeshBufferData.meshletTrianglesBufferOffset = m_meshletTrianglesBufferView->GetOffset();
	auto& resourceManager = ResourceManager::GetInstance();
	resourceManager.UpdateConstantBuffer(m_pPerMeshBuffer, m_perMeshBufferData);
}

void Mesh::SetBufferViews(std::unique_ptr<BufferView> vertexBufferView, std::unique_ptr<BufferView> meshletBufferView, std::unique_ptr<BufferView> meshletVerticesBufferView, std::unique_ptr<BufferView> meshletTrianglesBufferView) {
	m_vertexBufferView2 = std::move(vertexBufferView);
	m_meshletBufferView = std::move(meshletBufferView);
	m_meshletVerticesBufferView = std::move(meshletVerticesBufferView);
	m_meshletTrianglesBufferView = std::move(meshletTrianglesBufferView);
	m_perMeshBufferData.vertexBufferOffset = m_vertexBufferView2->GetOffset();
	m_perMeshBufferData.meshletBufferOffset = m_meshletBufferView->GetOffset() / sizeof(meshopt_Meshlet);
	m_perMeshBufferData.meshletVerticesBufferOffset = m_meshletVerticesBufferView->GetOffset() / 4;
	m_perMeshBufferData.meshletTrianglesBufferOffset = m_meshletTrianglesBufferView->GetOffset();
	auto& resourceManager = ResourceManager::GetInstance();
	resourceManager.UpdateConstantBuffer(m_pPerMeshBuffer, m_perMeshBufferData);
}