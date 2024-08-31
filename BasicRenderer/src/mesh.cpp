#include "Mesh.h"
#include "DirectX/d3dx12.h"
#include "Utilities.h"
#include "DeviceManager.h"
#include "PSOFlags.h"

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, bool skinned) {
    CreateBuffers(vertices, indices);
    this->material = material;
    m_psoFlags = material->psoFlags;
    if (skinned) {
        m_psoFlags |= PSOFlags::SKINNED;
    }
    auto& resourceManager = ResourceManager::GetInstance();
    m_perMeshBufferData.materialDataIndex = material->GetMaterialBufferIndex();
    m_pPerMeshBuffer = CreateConstantBuffer<PerMeshCB>(&m_perMeshBufferData);
}

template <typename VertexType>
void Mesh::CreateVertexBuffer(const std::vector<VertexType>& vertices, ComPtr<ID3D12Resource>& vertexBuffer) {
    
    const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(VertexType));

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)));

    UINT8* pVertexDataBegin;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
    memcpy(pVertexDataBegin, vertices.data(), vertexBufferSize);
    vertexBuffer->Unmap(0, nullptr);

    m_vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
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
        CreateVertexBuffer(specificVertices, m_vertexBuffer);
        }, vertices.front());

    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(UINT32));
    m_indexCount = indices.size();

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RANGE readRange(0, 0);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);

    auto& device = DeviceManager::GetInstance().GetDevice();
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_indexBuffer)));

    UINT8* pIndexDataBegin;
    ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
    memcpy(pIndexDataBegin, indices.data(), indexBufferSize);
    m_indexBuffer->Unmap(0, nullptr);

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;
}

D3D12_VERTEX_BUFFER_VIEW Mesh::GetVertexBufferView() const {
    return m_vertexBufferView;
}

D3D12_INDEX_BUFFER_VIEW Mesh::GetIndexBufferView() const {
    return m_indexBufferView;
}

ComPtr<ID3D12Resource> Mesh::GetPerMeshBuffer() const {
    return m_pPerMeshBuffer;
}

UINT Mesh::GetIndexCount() const {
    return m_indexCount;
}

UINT Mesh::GetPSOFlags() const {
    return m_psoFlags;
}