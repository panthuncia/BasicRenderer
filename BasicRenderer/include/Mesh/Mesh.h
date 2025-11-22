#pragma once

#include <directxmath.h>
#include <vector>
#include <memory>
#include <atomic>
#include <optional>
#include <rhi.h>

#include "Mesh/VertexFlags.h"
#include "Import/MeshData.h"
#include "ShaderBuffers.h"
#include "meshoptimizer.h"
#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/DeletionManager.h"

class Material;
class MeshManager;
class Skeleton;
class Buffer;

class Mesh {
public:
	~Mesh()
	{
		auto& deletionManager = DeletionManager::GetInstance();
		deletionManager.MarkForDelete(m_vertexBufferHandle);
		deletionManager.MarkForDelete(m_indexBufferHandle);
	}
	static std::shared_ptr<Mesh> CreateShared(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags) {
		return std::shared_ptr<Mesh>(new Mesh(std::move(vertices), vertexSize, std::move(skinningVertices), skinningVertexSize, indices, material, flags));
    }
	uint64_t GetNumVertices(bool meshletReorderedVertices) const {
		uint64_t size = meshletReorderedVertices ? m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize : m_vertices->size() / m_perMeshBufferData.vertexByteSize;
		return size;
	}
    rhi::VertexBufferView GetVertexBufferView() const;
    rhi::IndexBufferView GetIndexBufferView() const;
	PerMeshCB& GetPerMeshCBData() { return m_perMeshBufferData; };
    UINT GetIndexCount() const;
	uint64_t GetGlobalID() const;
	std::vector<std::byte>& GetVertices() { return *m_vertices; }
	std::vector<std::byte>& GetMeshletReorderedVertices() { return m_meshletReorderedVertices; }
	std::vector<std::byte>& GetSkinningVertices() { return *m_skinningVertices; }
	std::vector<std::byte>& GetMeshletReorderedSkinningVertices() { return m_meshletReorderedSkinningVertices; }
	std::vector<meshopt_Meshlet>& GetMeshlets() { return m_meshlets; }
	std::vector<unsigned int>& GetMeshletVertices() { return m_meshletVertices; }
	std::vector<unsigned char>& GetMeshletTriangles() { return m_meshletTriangles; }

    std::shared_ptr<Material> material;

	void SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view);
	BufferView* GetPreSkinningVertexBufferView();
	BufferView* GetPostSkinningVertexBufferView();
	void SetMeshletOffsetsBufferView(std::unique_ptr<BufferView> view);
	void SetMeshletVerticesBufferView(std::unique_ptr<BufferView> view);
	void SetMeshletTrianglesBufferView(std::unique_ptr<BufferView> view);

	BufferView* GetMeshletOffsetsBufferView() { return m_meshletBufferView.get(); }
	BufferView* GetMeshletVerticesBufferView() { return m_meshletVerticesBufferView.get(); }
	BufferView* GetMeshletTrianglesBufferView() { return m_meshletTrianglesBufferView.get(); }

	void SetBufferViews(std::unique_ptr<BufferView> preSkinningVertexBufferView, std::unique_ptr<BufferView> postSkinningVertexBufferView, std::unique_ptr<BufferView> meshletBufferView, std::unique_ptr<BufferView> meshletVerticesBufferView, std::unique_ptr<BufferView> meshletTrianglesBufferView, std::unique_ptr<BufferView> meshletBoundsBufferView);
	void SetBaseSkin(std::shared_ptr<Skeleton> skeleton);
	bool HasBaseSkin() const { return m_baseSkeleton != nullptr; }
	std::shared_ptr<Skeleton> GetBaseSkin() const { return m_baseSkeleton; }

	uint64_t GetMeshletBufferOffset() const {
		return m_meshletBufferView->GetOffset();
	}

	uint64_t GetMeshletVerticesBufferOffset() const {
		return m_meshletVerticesBufferView->GetOffset();
	}

	uint64_t GetMeshletTrianglesBufferOffset() const {
		return m_meshletTrianglesBufferView->GetOffset();
	}

	uint32_t GetMeshletCount() {
		return static_cast<uint32_t>(m_meshlets.size()); // TODO: support meshes with >32 bit int meshlets?
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

	unsigned int GetSkinningVertexSize() const {
		return m_skinningVertexSize;
	}

	void UpdateVertexCount(bool meshletReorderedVertices);

	std::vector<BoundingSphere>& GetMeshletBounds() {
		return m_meshletBounds;
	}

	void SetMeshletBoundsBufferView(std::unique_ptr<BufferView> view) {
		m_meshletBoundsBufferView = std::move(view);
	}

	const BufferView* GetMeshletBoundsBufferView() {
		return m_meshletBoundsBufferView.get();
	}

private:
    Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material>, unsigned int flags);
    void CreateVertexBuffer();
    void CreateMeshlets(const std::vector<UINT32>& indices);
	void CreateMeshletReorderedVertices();
    void CreateBuffers(const std::vector<UINT32>& indices);
	void ComputeBoundingSphere(const std::vector<UINT32>& indices);
	void ComputeAABB(DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max);
    static int GetNextGlobalIndex();

    static std::atomic<uint32_t> globalMeshCount;
	uint32_t m_globalMeshID;

	std::unique_ptr<std::vector<std::byte>> m_vertices;
	std::unique_ptr<std::vector<std::byte>> m_skinningVertices;
	std::vector<meshopt_Meshlet> m_meshlets;
	std::vector<unsigned int> m_meshletVertices;
    std::vector<unsigned char> m_meshletTriangles;
	std::vector<BoundingSphere> m_meshletBounds;
	std::vector<std::byte> m_meshletReorderedVertices;
	std::vector<std::byte> m_meshletReorderedSkinningVertices;

    std::unique_ptr<BufferView> m_postSkinningVertexBufferView = nullptr;
	std::unique_ptr<BufferView> m_preSkinningVertexBufferView = nullptr;
	std::unique_ptr<BufferView> m_meshletBufferView = nullptr;
	std::unique_ptr<BufferView> m_meshletVerticesBufferView = nullptr;
	std::unique_ptr<BufferView> m_meshletTrianglesBufferView = nullptr;

	UINT m_indexCount = 0;
    std::shared_ptr<Buffer> m_vertexBufferHandle;
	std::shared_ptr<Buffer> m_indexBufferHandle;
    rhi::VertexBufferView m_vertexBufferView;
    rhi::IndexBufferView m_indexBufferView;

    PerMeshCB m_perMeshBufferData = { 0 };
	unsigned int m_skinningVertexSize = 0;
	std::unique_ptr<BufferView> m_perMeshBufferView;
	std::unique_ptr<BufferView> m_meshletBoundsBufferView;
	MeshManager* m_pCurrentMeshManager = nullptr;

	std::shared_ptr<Skeleton> m_baseSkeleton = nullptr;
};