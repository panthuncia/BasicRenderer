#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <directxmath.h>
#include <vector>

using namespace Microsoft::WRL;

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
};

class Mesh {
public:
    Mesh(ID3D12Device* device, const std::vector<Vertex>& vertices, const std::vector<UINT16>& indices);
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const;
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() const;
    UINT GetIndexCount() const;

private:
    void CreateBuffers(ID3D12Device* device, const std::vector<Vertex>& vertices, const std::vector<UINT16>& indices);

    UINT indexCount = 0;
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;
};