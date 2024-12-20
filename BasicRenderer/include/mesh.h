#pragma once

#include <wrl.h>
#include <directx/d3d12.h>
#include <directxmath.h>
#include <vector>
#include <memory>
#include <atomic>

#include "Vertex.h"
#include "MeshData.h"
#include "buffers.h"
#include "meshoptimizer.h"
#include "BufferView.h"
using namespace Microsoft::WRL;

class Material;
class MeshManager;

class Mesh {
public:
    static std::shared_ptr<Mesh> CreateShared(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags) {
		return std::shared_ptr<Mesh>(new Mesh(vertices, indices, material, flags));
    }
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const;
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() const;
	PerMeshCB& GetPerMeshCBData() { return m_perMeshBufferData; };
    UINT GetIndexCount() const;
	int GetGlobalID() const;
	std::vector<Vertex>& GetVertices() { return m_vertices; }
	std::vector<meshopt_Meshlet>& GetMeshlets() { return m_meshlets; }
	std::vector<unsigned int>& GetMeshletVertices() { return m_meshletVertices; }
	std::vector<unsigned char>& GetMeshletTriangles() { return m_meshletTriangles; }

    std::shared_ptr<Material> material;

	void SetVertexBufferView(std::unique_ptr<BufferView> view);
	void SetMeshletOffsetsBufferView(std::unique_ptr<BufferView> view);
	void SetMeshletVerticesBufferView(std::unique_ptr<BufferView> view);
	void SetMeshletTrianglesBufferView(std::unique_ptr<BufferView> view);

	void SetBufferViews(std::unique_ptr<BufferView> vertexBufferView, std::unique_ptr<BufferView> meshletBufferView, std::unique_ptr<BufferView> meshletVerticesBufferView, std::unique_ptr<BufferView> meshletTrianglesBufferView);

	unsigned int GetVertexBufferOffset() const {
		return m_vertexBufferView2->GetOffset();
	}

	unsigned int GetMeshletBufferOffset() const {
		return m_meshletBufferView->GetOffset();
	}

	unsigned int GetMeshletVerticesBufferOffset() const {
		return m_meshletVerticesBufferView->GetOffset();
	}

	unsigned int GetMeshletTrianglesBufferOffset() const {
		return m_meshletTrianglesBufferView->GetOffset();
	}

	unsigned int GetMeshletCount() {
		return m_meshlets.size();
	}

	void SetPerMeshBufferView(std::unique_ptr<BufferView> view) {
		m_perMeshBufferView = std::move(view);
	}

	std::unique_ptr<BufferView>& GetPerMeshBufferView() {
		return m_perMeshBufferView;
	}

	void SetCurrentMeshManager(MeshManager* manager) {
		m_pCurrentMeshManager = manager;
	}

private:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices, const std::shared_ptr<Material>, unsigned int flags);
    template <typename VertexType>
    void CreateVertexBuffer(const std::vector<VertexType>& vertices);
    template <typename VertexType>
    void CreateMeshlets(const std::vector<VertexType>& vertices, const std::vector<UINT32>& indices);
    void CreateBuffers(const std::vector<Vertex>& vertices, const std::vector<UINT32>& indices);
	template <typename VertexType>
	void ComputeBoundingSphere(const std::vector<VertexType>& vertices, const std::vector<UINT32>& indices);
	template <typename VertexType>
	void ComputeAABB(const std::vector<VertexType>& vertices, DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max);
    static int GetNextGlobalIndex();

    static std::atomic<int> globalMeshCount;
    int m_globalMeshID;

	std::vector<Vertex> m_vertices;
	std::vector<meshopt_Meshlet> m_meshlets;
	std::vector<unsigned int> m_meshletVertices;
    std::vector<unsigned char> m_meshletTriangles;

    std::unique_ptr<BufferView> m_vertexBufferView2 = nullptr;
	std::unique_ptr<BufferView> m_meshletBufferView = nullptr;
	std::unique_ptr<BufferView> m_meshletVerticesBufferView = nullptr;
	std::unique_ptr<BufferView> m_meshletTrianglesBufferView = nullptr;

	UINT m_indexCount = 0;
    std::shared_ptr<Buffer> m_vertexBufferHandle;
	std::shared_ptr<Buffer> m_indexBufferHandle;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    PerMeshCB m_perMeshBufferData = { 0 };
	std::unique_ptr<BufferView> m_perMeshBufferView;
	MeshManager* m_pCurrentMeshManager = nullptr;
};