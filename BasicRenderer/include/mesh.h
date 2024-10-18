#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <directxmath.h>
#include <vector>
#include <memory>
#include <atomic>

#include "Vertex.h"
#include "BufferHandle.h"
#include "MeshData.h"
#include "buffers.h"
#include "meshoptimizer.h"
using namespace Microsoft::WRL;

class Material;

class Mesh {
public:
    static std::shared_ptr<Mesh> CreateShared(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, bool skinned = false) {
		return std::shared_ptr<Mesh>(new Mesh(vertices, indices, material, skinned));
    }
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const;
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() const;
    BufferHandle& GetPerMeshBuffer();
    UINT GetIndexCount() const;
    UINT GetPSOFlags() const;
	int GetGlobalID() const;
    std::shared_ptr<Material> material;

private:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material>, bool skinned);
    template <typename VertexType>
    void CreateVertexBuffer(const std::vector<VertexType>& vertices);
    template <typename VertexType>
    void CreateMeshlets(const std::vector<VertexType>& vertices, const std::vector<UINT32>& indices);
    void CreateBuffers(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices);
    static int GetNextGlobalIndex();

    static std::atomic<int> globalMeshCount;
    int m_globalMeshID;

	std::vector<meshopt_Meshlet> m_meshlets;
	std::vector<unsigned int> m_meshletVertices;
    std::vector<unsigned char> m_meshletTriangles;
    UINT m_psoFlags = 0;
    UINT m_indexCount = 0;
    BufferHandle m_vertexBufferHandle;
	BufferHandle m_indexBufferHandle;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    BufferHandle m_pPerMeshBuffer;
    PerMeshCB m_perMeshBufferData = { 0 };
};