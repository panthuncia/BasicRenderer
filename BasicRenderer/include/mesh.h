#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <directxmath.h>
#include <vector>
#include <memory>

#include "Vertex.h"
#include "Material.h"

using namespace Microsoft::WRL;

struct GeometryData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint32_t> indices;
    std::vector<float> texcoords;
    std::vector<float> joints;
    std::vector<float> weights;
    std::shared_ptr<Material> material;
};

struct MeshData {
    std::vector<GeometryData> geometries;
};


class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material>, bool skinned);
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const;
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() const;
    ComPtr<ID3D12Resource> GetPerMeshBuffer() const;
    UINT GetIndexCount() const;
    UINT GetPSOFlags() const;

    std::shared_ptr<Material> material;

private:
    template <typename VertexType>
    void CreateVertexBuffer(const std::vector<VertexType>& vertices, ComPtr<ID3D12Resource>& vertexBuffer);
    void CreateBuffers(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices);

    UINT m_psoFlags = 0;
    UINT m_indexCount = 0;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    ComPtr<ID3D12Resource> m_pPerMeshBuffer;
    PerMeshCB m_perMeshBufferData = { 0 };
};