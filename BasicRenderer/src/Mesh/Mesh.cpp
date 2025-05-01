#include "Mesh/Mesh.h"

#include <meshoptimizer.h>

#include "DirectX/d3dx12.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/PSOFlags.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/Material.h"
#include "Mesh/VertexFlags.h"
#include "Managers/MeshManager.h"
#include "Animation/Skeleton.h"

std::atomic<uint64_t> Mesh::globalMeshCount = 0;

Mesh::Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags) {
    m_vertices = std::move(vertices);
	if (skinningVertices.has_value()) {
		m_skinningVertices = std::move(skinningVertices.value());
	}
	m_perMeshBufferData.materialDataIndex = material->GetMaterialBufferIndex();
	m_perMeshBufferData.vertexFlags = flags;
	m_perMeshBufferData.vertexByteSize = vertexSize;
	m_perMeshBufferData.numVertices = m_vertices->size() / vertexSize;
	m_perMeshBufferData.skinningVertexByteSize = skinningVertexSize;

	m_skinningVertexSize = skinningVertexSize;
	CreateBuffers(indices);
    this->material = material;

	m_globalMeshID = GetNextGlobalIndex();
}

void Mesh::CreateVertexBuffer() {
    
    const UINT vertexBufferSize = static_cast<UINT>(m_vertices->size());

	m_vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, (void*)m_vertices->data());

    m_vertexBufferView.BufferLocation = m_vertexBufferHandle->m_buffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(m_perMeshBufferData.vertexByteSize);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;
}

void Mesh::CreateMeshlets(const std::vector<UINT32>& indices) {
	unsigned int maxVertices = 64;
	unsigned int maxPrimitives = 64;

	size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxPrimitives);

	m_meshlets.resize(maxMeshlets);
	m_meshletVertices.resize(maxMeshlets * maxVertices);
	m_meshletTriangles.resize(maxMeshlets * maxPrimitives * 3);

    m_meshlets = std::vector<meshopt_Meshlet>(maxMeshlets);
	m_meshletVertices = std::vector<unsigned int>(maxMeshlets*maxVertices);
	m_meshletTriangles = std::vector<unsigned char>(maxMeshlets * maxPrimitives * 3);
    
	auto meshletCount = meshopt_buildMeshlets(m_meshlets.data(), m_meshletVertices.data(), m_meshletTriangles.data(), indices.data(), indices.size(), (float*)m_vertices->data(), m_vertices->size()/m_perMeshBufferData.vertexByteSize, m_perMeshBufferData.vertexByteSize, maxVertices, maxPrimitives, 0);
	m_meshlets.resize(meshletCount);
}

void Mesh::ComputeAABB(DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max) {
	// Initialize min and max vectors
	min.x = min.y = min.z = std::numeric_limits<float>::infinity();
	max.x = max.y = max.z = -std::numeric_limits<float>::infinity();

	unsigned int positionsByteStride = m_perMeshBufferData.vertexByteSize;
	unsigned int positionsOffset = 0;
	for (unsigned int i = 0; i < m_vertices->size(); i += positionsByteStride) {
		XMFLOAT3* position = (XMFLOAT3*)(m_vertices->data() + i + positionsOffset);
		min.x = (std::min)(min.x, position->x);
		min.y = (std::min)(min.y, position->y);
		min.z = (std::min)(min.z, position->z);
		max.x = (std::max)(max.x, position->x);
		max.y = (std::max)(max.y, position->y);
		max.z = (std::max)(max.z, position->z);
	}
}

void Mesh::ComputeBoundingSphere(const std::vector<UINT32>& indices) {
	BoundingSphere sphere = {};

	XMFLOAT3 min, max;

	// Compute the Axis-Aligned Bounding Box
	ComputeAABB(min, max);
	XMFLOAT3 center = Scale(Add(min, max), 0.5f);

	// Compute the radius of the bounding sphere
	XMFLOAT3 diagonal = Subtract(max, min);
	float radius = 0.5f * sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y + diagonal.z * diagonal.z);

	// Set the bounding sphere
	sphere.center = DirectX::XMFLOAT4(center.x, center.y, center.z, 1.0);
	sphere.radius = radius;

	m_perMeshBufferData.boundingSphere = sphere;
}

void Mesh::CreateMeshletReorderedVertices() {
	const size_t meshletCount    = m_meshlets.size();
	const size_t vertexByteSize  = m_perMeshBufferData.vertexByteSize;

	size_t totalVerts = 0;
	for (auto& ml : m_meshlets) {
		totalVerts += ml.vertex_count;
	}

	m_meshletReorderedVertices.resize(totalVerts * vertexByteSize);
	m_meshletReorderedSkinningVertices.resize(totalVerts * m_skinningVertexSize);

	// Post-skinning vertices
	std::byte* dst = m_meshletReorderedVertices.data();
	for (const auto& ml : m_meshlets) {
		for (unsigned int i = 0; i < ml.vertex_count; ++i) {
			unsigned int globalIndex = 
				m_meshletVertices[ml.vertex_offset + i];

			std::byte* src = m_vertices->data()
				+ globalIndex * vertexByteSize;

			std::copy_n(src, vertexByteSize, dst);
			dst += vertexByteSize;
		}
	}

	// Skinning vertices, if we have them
	std::byte* dstSkinning = m_meshletReorderedSkinningVertices.data();
	if (m_skinningVertices) {
		for (const auto& ml : m_meshlets) {
			for (unsigned int i = 0; i < ml.vertex_count; ++i) {
				unsigned int globalIndex =
					m_meshletVertices[ml.vertex_offset + i];
				std::byte* src = m_skinningVertices->data()
					+ globalIndex * m_skinningVertexSize;
				std::copy_n(src, m_skinningVertexSize, dstSkinning);
				dstSkinning += m_skinningVertexSize;
			}
		}
	}
}

void Mesh::CreateBuffers(const std::vector<UINT32>& indices) {

	CreateMeshlets(indices);
	CreateMeshletReorderedVertices();
    CreateVertexBuffer();
	ComputeBoundingSphere(indices);

    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(UINT32));
    m_indexCount = indices.size();

	m_indexBufferHandle = ResourceManager::GetInstance().CreateBuffer(indexBufferSize, (void*)indices.data());

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

uint64_t Mesh::GetGlobalID() const {
	return m_globalMeshID;
}

void Mesh::SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_preSkinningVertexBufferView = std::move(view);
	m_perMeshBufferData.vertexBufferOffset = m_preSkinningVertexBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

BufferView* Mesh::GetPreSkinningVertexBufferView() {
	return m_preSkinningVertexBufferView.get();
}

BufferView* Mesh::GetPostSkinningVertexBufferView() {
	return m_postSkinningVertexBufferView.get();
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

void Mesh::SetBufferViews(std::unique_ptr<BufferView> preSkinningVertexBufferView, std::unique_ptr<BufferView> postSkinningVertexBufferView, std::unique_ptr<BufferView> meshletBufferView, std::unique_ptr<BufferView> meshletVerticesBufferView, std::unique_ptr<BufferView> meshletTrianglesBufferView) {
	m_postSkinningVertexBufferView = std::move(postSkinningVertexBufferView);
	m_preSkinningVertexBufferView = std::move(preSkinningVertexBufferView);
	m_meshletBufferView = std::move(meshletBufferView);
	m_meshletVerticesBufferView = std::move(meshletVerticesBufferView);
	m_meshletTrianglesBufferView = std::move(meshletTrianglesBufferView);
	if (m_meshletBufferView == nullptr || m_meshletVerticesBufferView == nullptr || m_meshletTrianglesBufferView == nullptr) {
		return; // We're probably deleting the mesh
	}
	if (m_preSkinningVertexBufferView != nullptr) { // If the mesh is skinned
		m_perMeshBufferData.vertexBufferOffset = m_preSkinningVertexBufferView->GetOffset();
	}
	else { // If the mesh is not skinned
		m_perMeshBufferData.vertexBufferOffset = m_postSkinningVertexBufferView->GetOffset();
	}
	m_perMeshBufferData.meshletBufferOffset = m_meshletBufferView->GetOffset() / sizeof(meshopt_Meshlet);
	m_perMeshBufferData.meshletVerticesBufferOffset = m_meshletVerticesBufferView->GetOffset() / 4;
	m_perMeshBufferData.meshletTrianglesBufferOffset = m_meshletTrianglesBufferView->GetOffset();

	if (m_pCurrentMeshManager != nullptr && m_perMeshBufferView != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetBaseSkin(std::shared_ptr<Skeleton> skeleton) {
	m_baseSkeleton = skeleton;
	//m_perMeshBufferData.boneTransformBufferIndex = skeleton->GetTransformsBufferIndex();
	m_perMeshBufferData.inverseBindMatricesBufferIndex = skeleton->GetInverseBindMatricesBufferIndex();
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
	//skeleton->userIDs.push_back(localID);
}

void Mesh::UpdateVertexCount(bool meshletReorderedVertices) {
	m_perMeshBufferData.numVertices = meshletReorderedVertices ? m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize : m_vertices->size() / m_perMeshBufferData.vertexByteSize;
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}