#include "Mesh.h"
#include "DirectX/d3dx12.h"
#include "Utilities.h"
#include "DeviceManager.h"
#include "PSOFlags.h"
#include "ResourceManager.h"

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, bool skinned) {
    CreateBuffers(vertices, indices);
    this->material = material;
    m_psoFlags = material->m_psoFlags;
    if (skinned) {
        m_psoFlags |= PSOFlags::SKINNED;
    }
    auto& resourceManager = ResourceManager::GetInstance();
    m_perMeshBufferData.materialDataIndex = material->GetMaterialBufferIndex();
    m_pPerMeshBuffer = resourceManager.CreateConstantBuffer<PerMeshCB>(L"PerMeshCB");
	resourceManager.UpdateConstantBuffer(m_pPerMeshBuffer, m_perMeshBufferData);
}

template <typename VertexType>
void Mesh::CreateVertexBuffer(const std::vector<VertexType>& vertices) {
    
    const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(VertexType));

	m_vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, ResourceUsageType::VERTEX, (void*)vertices.data());

    m_vertexBufferView.BufferLocation = m_vertexBufferHandle.dataBuffer->m_buffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(VertexType);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;
}

void Mesh::CreateBuffers(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices) {

    std::visit([&](auto&& vertex) {
        using T = std::decay_t<decltype(vertex)>;
        std::vector<T> specificVertices;
        specificVertices.reserve(vertices.size());
        for (const auto& v : vertices) {
            specificVertices.push_back(std::get<T>(v));
        }
        CreateVertexBuffer(specificVertices);
        }, vertices.front());

    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(UINT32));
    m_indexCount = indices.size();

	m_indexBufferHandle = ResourceManager::GetInstance().CreateBuffer(indexBufferSize, ResourceUsageType::INDEX, (void*)indices.data());

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

UINT Mesh::GetPSOFlags() const {
    return m_psoFlags;
}