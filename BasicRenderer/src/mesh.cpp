#include "Mesh.h"

#include <meshoptimizer.h>

#include "DirectX/d3dx12.h"
#include "Utilities.h"
#include "DeviceManager.h"
#include "PSOFlags.h"
#include "ResourceManager.h"
#include "Material.h"
#include "Vertex.h"
#include "MeshManager.h"

std::atomic<int> Mesh::globalMeshCount = 0;

Mesh::Mesh(std::unique_ptr<std::vector<std::byte>> vertices, std::unique_ptr<std::vector<SkinningVertex>> skinningVertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags) {
    m_vertices = std::move(vertices);
    CreateBuffers(indices);
    this->material = material;
    auto& resourceManager = ResourceManager::GetInstance();
    m_perMeshBufferData.materialDataIndex = material->GetMaterialBufferIndex();
	m_perMeshBufferData.vertexFlags = flags;
	std::visit([&](auto&& vertex) { // Get byte size of vertices
        using T = std::decay_t<decltype(vertex)>;
		m_perMeshBufferData.vertexByteSize = sizeof(T);
		}, vertices.front());
    //m_pPerMeshBuffer = resourceManager.CreateConstantBuffer<PerMeshCB>(L"PerMeshCB");
	//resourceManager.UpdateConstantBuffer(m_pPerMeshBuffer, m_perMeshBufferData);
	m_globalMeshID = GetNextGlobalIndex();
}

template <typename VertexType>
void Mesh::CreateVertexBuffer(const std::vector<VertexType>& vertices) {
    
    const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(VertexType));

	m_vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, ResourceState::VERTEX, (void*)vertices.data());

    m_vertexBufferView.BufferLocation = m_vertexBufferHandle->m_buffer->GetGPUVirtualAddress();
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

template <typename VertexType>
void Mesh::ComputeAABB(const std::vector<VertexType>& vertices, DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max) {
	// Initialize min and max vectors
	min.x = min.y = min.z = std::numeric_limits<float>::infinity();
	max.x = max.y = max.z = -std::numeric_limits<float>::infinity();

	// Loop through each vertex and update min and max
	for (const auto& v : vertices) {
		// Update min
		min.x = (std::min)(min.x, v.position.x);
		min.y = (std::min)(min.y, v.position.y);
		min.z = (std::min)(min.z, v.position.z);

		// Update max
		max.x = (std::max)(max.x, v.position.x);
		max.y = (std::max)(max.y, v.position.y);
		max.z = (std::max)(max.z, v.position.z);
	}
}

template <typename VertexType>
void Mesh::ComputeBoundingSphere(const std::vector<VertexType>& vertices, const std::vector<UINT32>& indices) {
	BoundingSphere sphere = {};

	XMFLOAT3 min, max;

	// Compute the Axis-Aligned Bounding Box
	ComputeAABB(vertices, min, max);
	XMFLOAT3 center = Scale(Add(min, max), 0.5f);

	// Compute the radius of the bounding sphere
	XMFLOAT3 diagonal = Subtract(max, min);
	float radius = 0.5f * sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y + diagonal.z * diagonal.z);

	// Set the bounding sphere
	sphere.center = DirectX::XMFLOAT4(center.x, center.y, center.z, 1.0);
	sphere.radius = radius;

	//for (const auto& v : vertices) {
	//	sphere.center.x += v.position.x;
	//	sphere.center.y += v.position.y;
	//	sphere.center.z += v.position.z;
	//}
	//sphere.center.x /= vertices.size();
	//sphere.center.y /= vertices.size();
	//sphere.center.z /= vertices.size();

	//for (const auto& v : vertices) {
	//	float dx = v.position.x - sphere.center.x;
	//	float dy = v.position.y - sphere.center.y;
	//	float dz = v.position.z - sphere.center.z;
	//	float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
	//	sphere.radius = (std::max)(sphere.radius, distance);
	//}	
	m_perMeshBufferData.boundingSphere = sphere;
}

void Mesh::CreateBuffers(const std::vector<UINT32>& indices) {

    std::visit([&](auto&& vertex) {
        using T = std::decay_t<decltype(vertex)>;
        std::vector<T> specificVertices;
        specificVertices.reserve(vertices.size());
        for (const auto& v : vertices) {
            specificVertices.push_back(std::get<T>(v));
        }
		CreateMeshlets(specificVertices, indices);
        CreateVertexBuffer(specificVertices);
		ComputeBoundingSphere(specificVertices, indices);
        }, vertices.front());

    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(UINT32));
    m_indexCount = indices.size();

	m_indexBufferHandle = ResourceManager::GetInstance().CreateBuffer(indexBufferSize, ResourceState::INDEX, (void*)indices.data());

    m_indexBufferView.BufferLocation = m_indexBufferHandle->m_buffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;
}

D3D12_VERTEX_BUFFER_VIEW Mesh::GetVertexBufferView() const {
    return m_vertexBufferView;
}

D3D12_INDEX_BUFFER_VIEW Mesh::GetIndexBufferView() const {
    return m_indexBufferView;
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

void Mesh::SetPostSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_postSkinningVertexBufferView = std::move(view);
	m_perMeshBufferData.postSkinningVertexBufferOffset = m_postSkinningVertexBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_preSkinningVertexBufferView = std::move(view);
	m_perMeshBufferData.preSkinningVertexBufferOffset = m_preSkinningVertexBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

BufferView* Mesh::GetPostSkinningVertexBufferView() {
	return m_postSkinningVertexBufferView.get();
}

BufferView* Mesh::GetPreSkinningVertexBufferView() {
	return m_preSkinningVertexBufferView.get();
}

void Mesh::SetMeshletOffsetsBufferView(std::unique_ptr<BufferView> view) {
	m_meshletBufferView = std::move(view);
	m_perMeshBufferData.meshletBufferOffset = m_meshletBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}
void Mesh::SetMeshletVerticesBufferView(std::unique_ptr<BufferView> view) {
	m_meshletVerticesBufferView = std::move(view);
	m_perMeshBufferData.meshletVerticesBufferOffset = m_meshletVerticesBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}
void Mesh::SetMeshletTrianglesBufferView(std::unique_ptr<BufferView> view) {
	m_meshletTrianglesBufferView = std::move(view);
	m_perMeshBufferData.meshletTrianglesBufferOffset = m_meshletTrianglesBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetBufferViews(std::unique_ptr<BufferView> postSkinningVertexBufferView, std::unique_ptr<BufferView> preSkinningVertexBufferView, std::unique_ptr<BufferView> meshletBufferView, std::unique_ptr<BufferView> meshletVerticesBufferView, std::unique_ptr<BufferView> meshletTrianglesBufferView) {
	m_postSkinningVertexBufferView = std::move(postSkinningVertexBufferView);
	m_preSkinningVertexBufferView = std::move(preSkinningVertexBufferView);
	m_meshletBufferView = std::move(meshletBufferView);
	m_meshletVerticesBufferView = std::move(meshletVerticesBufferView);
	m_meshletTrianglesBufferView = std::move(meshletTrianglesBufferView);
	m_perMeshBufferData.postSkinningVertexBufferOffset = m_postSkinningVertexBufferView->GetOffset();
	if (m_preSkinningVertexBufferView != nullptr) {
		m_perMeshBufferData.preSkinningVertexBufferOffset = m_preSkinningVertexBufferView->GetOffset();
	}
	m_perMeshBufferData.meshletBufferOffset = m_meshletBufferView->GetOffset() / sizeof(meshopt_Meshlet);
	m_perMeshBufferData.meshletVerticesBufferOffset = m_meshletVerticesBufferView->GetOffset() / 4;
	m_perMeshBufferData.meshletTrianglesBufferOffset = m_meshletTrianglesBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}