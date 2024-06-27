#include "mesh.h"
#include "DirectX/d3dx12.h"
#include "utilities.h"

Mesh::Mesh(ID3D12Device* device, const std::vector<Vertex>& vertices, const std::vector<UINT16>& indices) {
    CreateBuffers(device, vertices, indices);
}

void Mesh::CreateBuffers(ID3D12Device* device, const std::vector<Vertex>& vertices, const std::vector<UINT16>& indices) {
    const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(UINT16));
    indexCount = indices.size();

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

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

    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = vertexBufferSize;

    bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&indexBuffer)));

    UINT8* pIndexDataBegin;
    ThrowIfFailed(indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
    memcpy(pIndexDataBegin, indices.data(), indexBufferSize);
    indexBuffer->Unmap(0, nullptr);

    indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    indexBufferView.SizeInBytes = indexBufferSize;
}

D3D12_VERTEX_BUFFER_VIEW Mesh::GetVertexBufferView() const {
    return vertexBufferView;
}

D3D12_INDEX_BUFFER_VIEW Mesh::GetIndexBufferView() const {
    return indexBufferView;
}

UINT Mesh::GetIndexCount() const {
    return indexCount;
}