#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <directxmath.h>
#include <vector>
#include <memory>

#include "Vertex.h"
#include "Material.h"
#include "ResourceHandles.h"

using namespace Microsoft::WRL;

struct GeometryData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint32_t> indices;
    std::vector<float> texcoords;
    std::vector<UINT> joints;
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
    BufferHandle& GetPerMeshBuffer();
    UINT GetIndexCount() const;
    UINT GetPSOFlags() const;

    std::shared_ptr<Material> material;

private:
    template <typename VertexType>
    void CreateVertexBuffer(const std::vector<VertexType>& vertices);
    void CreateBuffers(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices);

    UINT m_psoFlags = 0;
    UINT m_indexCount = 0;
    BufferHandle m_vertexBufferHandle;
	BufferHandle m_indexBufferHandle;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    BufferHandle m_pPerMeshBuffer;
    PerMeshCB m_perMeshBufferData = { 0 };
};