#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <directxmath.h>
#include <vector>

#include "Vertex.h"

using namespace Microsoft::WRL;

struct GeometryData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint16_t> indices;
    std::vector<float> texcoords;
    std::vector<float> joints;
    std::vector<float> weights;
    int material;
};

struct MeshData {
    std::vector<GeometryData> geometries;
};


class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<UINT16>& indices);
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const;
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() const;
    UINT GetIndexCount() const;

private:
    template <typename VertexType>
    void CreateVertexBuffer(const std::vector<VertexType>& vertices, ComPtr<ID3D12Resource>& vertexBuffer);
    void CreateBuffers(const std::vector<Vertex>& vertices, const std::vector<UINT16>& indices);

    UINT indexCount = 0;
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;
};