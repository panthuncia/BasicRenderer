#pragma once

#include <directxmath.h>
#include <vector>
#include <memory>
#include <atomic>
#include <optional>
#include <rhi.h>
#include <meshoptimizer.h>
#include <ThirdParty/meshoptimizer/clusterlod.h>


#include "Mesh/VertexFlags.h"
#include "Import/MeshData.h"
#include "ShaderBuffers.h"
#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/DeletionManager.h"

class Material;
class MeshManager;
class Skeleton;
class Buffer;

struct ClusterLODTraversalMetric
{
	// Keep XYZ contiguous so meshopt_spatialClusterPoints can read float3 at &boundingSphereX
	float boundingSphereX = 0;
	float boundingSphereY = 0;
	float boundingSphereZ = 0;
	float boundingSphereRadius = 0;

	// Mirrors clodBounds::error / max quadric error
	float maxQuadricError = 0;
	float padding[3] = { 0,0,0 }; // Padding to make size multiple of 16 bytes
};

struct ClusterLODNodeRange
{
	// If isGroup=1: indexOrOffset = groupIndex, countMinusOne = groupMeshletCount-1
	// If isGroup=0: indexOrOffset = childOffset into m_clodNodes, countMinusOne = childCount-1
	uint32_t isGroup = 0;
	uint32_t indexOrOffset = 0;
	uint32_t countMinusOne = 0;
	uint32_t padding = 0;
};

struct ClusterLODNode
{
	ClusterLODNodeRange        range{};
	ClusterLODTraversalMetric  traversalMetric{};
};

struct ClusterLODNodeRangeAlloc
{
	uint32_t offset = 0;
	uint32_t count = 0;
};

class Mesh {
public:
	~Mesh()
	{
		auto& deletionManager = DeletionManager::GetInstance();
	}
	static std::shared_ptr<Mesh> CreateShared(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags) {
		return std::shared_ptr<Mesh>(new Mesh(std::move(vertices), vertexSize, std::move(skinningVertices), skinningVertexSize, indices, material, flags));
    }
	uint64_t GetNumVertices(bool meshletReorderedVertices) const {
		//uint64_t size = meshletReorderedVertices ? m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize : m_vertices->size() / m_perMeshBufferData.vertexByteSize;
		auto size = m_vertices->size() / m_perMeshBufferData.vertexByteSize;
		return size;
	}
	uint64_t GetStreamingNumVertices() const {
		if (!m_clodDuplicatedVertices.empty()) {
			return m_clodDuplicatedVertices.size() / m_perMeshBufferData.vertexByteSize;
		}
		return m_vertices->size() / m_perMeshBufferData.vertexByteSize;
	}
	uint32_t GetCLodGroupCount() const {
		return static_cast<uint32_t>(m_clodGroupChunks.size());
	}
    rhi::VertexBufferView GetVertexBufferView() const;
    rhi::IndexBufferView GetIndexBufferView() const;
	PerMeshCB& GetPerMeshCBData() { return m_perMeshBufferData; };
    UINT GetIndexCount() const;
	uint64_t GetGlobalID() const;
	std::vector<std::byte>& GetVertices() { return *m_vertices; }
	const std::vector<std::byte>& GetStreamingVertices() const {
		return m_clodDuplicatedVertices.empty() ? *m_vertices : m_clodDuplicatedVertices;
	}
	//std::vector<std::byte>& GetMeshletReorderedVertices() { return m_meshletReorderedVertices; }
	std::vector<std::byte>& GetSkinningVertices() { return *m_skinningVertices; }
	const std::vector<std::byte>& GetStreamingSkinningVertices() const {
		if (!m_clodDuplicatedSkinningVertices.empty()) {
			return m_clodDuplicatedSkinningVertices;
		}
		if (m_skinningVertices) {
			return *m_skinningVertices;
		}
		static const std::vector<std::byte> empty;
		return empty;
	}
	//std::vector<std::byte>& GetMeshletReorderedSkinningVertices() { return m_meshletReorderedSkinningVertices; }
	std::vector<meshopt_Meshlet>& GetMeshlets() { return m_meshlets; }
	std::vector<unsigned int>& GetMeshletVertices() { return m_meshletVertices; }
	std::vector<unsigned char>& GetMeshletTriangles() { return m_meshletTriangles; }

    std::shared_ptr<Material> material;

	void SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view);
	void SetPostSkinningVertexBufferView(std::unique_ptr<BufferView> view);
	BufferView* GetPreSkinningVertexBufferView();
	BufferView* GetPostSkinningVertexBufferView();
	void SetMeshletOffsetsBufferView(std::unique_ptr<BufferView> view);
	void SetMeshletVerticesBufferView(std::unique_ptr<BufferView> view);
	void SetMeshletTrianglesBufferView(std::unique_ptr<BufferView> view);

	void SetBaseSkin(std::shared_ptr<Skeleton> skeleton);
	bool HasBaseSkin() const { return m_baseSkeleton != nullptr; }
	std::shared_ptr<Skeleton> GetBaseSkin() const { return m_baseSkeleton; }

	uint32_t GetMeshletCount() {
		return static_cast<uint32_t>(m_meshlets.size()); // TODO: support meshes with >32 bit int meshlets?
	}

	uint32_t GetCLodMeshletCount() {
		return static_cast<uint32_t>(m_clodMeshlets.size());
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

	void SetMaterialDataIndex(unsigned int index);

	void SetCLodBufferViews(
		std::unique_ptr<BufferView> clusterLODGroupsView,
		std::unique_ptr<BufferView> clusterLODChildrenView,
		std::unique_ptr<BufferView> clusterLODMeshletsView,
		std::unique_ptr<BufferView> clusterLODMeshletVerticesView,
		std::unique_ptr<BufferView> clusterLODMeshletTrianglesView,
		std::unique_ptr<BufferView> clusterLODMeshletBoundsView,
		std::unique_ptr<BufferView> clodNodesView
	);

	const std::vector<ClusterLODGroup>& GetCLodGroups() const {
		return m_clodGroups;
	}

	const std::vector<ClusterLODChild>& GetCLodChildren() const {
		return m_clodChildren;
	}

	const std::vector<meshopt_Meshlet>& GetCLodMeshlets() const {
		return m_clodMeshlets;
	}

	const std::vector<uint32_t>& GetCLodMeshletVertices() const {
		return m_clodMeshletVertices;
	}

	const std::vector<uint8_t>& GetCLodMeshletTriangles() const {
		return m_clodMeshletTriangles;
	}

	const std::vector<BoundingSphere>& GetCLodBounds() const {
		return m_clodMeshletBounds;
	}

	const std::vector<ClusterLODGroupChunk>& GetCLodGroupChunks() const {
		return m_clodGroupChunks;
	}

	const std::vector<std::vector<std::byte>>& GetCLodGroupVertexChunks() const {
		return m_clodGroupVertexChunks;
	}

	const std::vector<std::vector<std::byte>>& GetCLodGroupSkinningVertexChunks() const {
		return m_clodGroupSkinningVertexChunks;
	}

	const std::vector<std::vector<uint32_t>>& GetCLodGroupMeshletVertexChunks() const {
		return m_clodGroupMeshletVertexChunks;
	}

	void ReleaseCLodChunkUploadData() {
		m_clodGroupVertexChunks.clear();
		m_clodGroupVertexChunks.shrink_to_fit();
		m_clodGroupSkinningVertexChunks.clear();
		m_clodGroupSkinningVertexChunks.shrink_to_fit();
		m_clodGroupMeshletVertexChunks.clear();
		m_clodGroupMeshletVertexChunks.shrink_to_fit();
	}

	const std::vector<ClusterLODNode>& GetCLodNodes() const {
		return m_clodNodes;
	}

	const BufferView* GetCLodGroupsView() const {
		return m_clusterLODGroupsView.get();
	}

	const BufferView* GetCLodChildrenView() const {
		return m_clusterLODChildrenView.get();
	}

	const BufferView* GetCLodMeshletsView() const {
		return m_clusterLODMeshletsView.get();
	}

	const BufferView* GetCLodMeshletVerticesView() const {
		return m_clusterLODMeshletVerticesView.get();
	}

	const BufferView* GetCLodMeshletTrianglesView() const {
		return m_clusterLODMeshletTrianglesView.get();
	}

	const BufferView* GetCLodMeshletBoundsView() const {
		return m_clusterLODMeshletBoundsView.get();
	}

	const BufferView* GetCLodNodesView() const {
		return m_clusterLODNodesView.get();
	}

	void SetCLodGroupChunkViews(
		std::vector<std::unique_ptr<BufferView>> preSkinningVertexChunkViews,
		std::vector<std::unique_ptr<BufferView>> postSkinningVertexChunkViews,
		std::vector<std::unique_ptr<BufferView>> meshletVertexChunkViews) {
		m_clodPreSkinningVertexChunkViews = std::move(preSkinningVertexChunkViews);
		m_clodPostSkinningVertexChunkViews = std::move(postSkinningVertexChunkViews);
		m_clodMeshletVertexChunkViews = std::move(meshletVertexChunkViews);
	}

	const std::vector<std::unique_ptr<BufferView>>& GetCLodPreSkinningVertexChunkViews() const {
		return m_clodPreSkinningVertexChunkViews;
	}

	const std::vector<std::unique_ptr<BufferView>>& GetCLodPostSkinningVertexChunkViews() const {
		return m_clodPostSkinningVertexChunkViews;
	}

	const std::vector<std::unique_ptr<BufferView>>& GetCLodMeshletVertexChunkViews() const {
		return m_clodMeshletVertexChunkViews;
	}

	uint32_t GetCLodRootNodeIndex() const { // For hierarchy cut
		return m_clodTopRootNode;
	}

	uint32_t GetCoarsestLODRootNodeIndex() const { // For DAG traversal
		return m_clodLodLevelRoots.back();
	}

private:
    Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material>, unsigned int flags);
    void CreateVertexBuffer();
    void CreateMeshlets(const std::vector<UINT32>& indices);
	//void CreateMeshletReorderedVertices();
    void CreateBuffers(const std::vector<UINT32>& indices);
	void ComputeBoundingSphere(const std::vector<UINT32>& indices);
	void ComputeAABB(DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max);
	void BuildClusterLOD(const std::vector<UINT32>& indices);
	void BuildClusterLODTraversalHierarchy(uint32_t preferredNodeWidth);
	void LogClusterLODHierarchyStats() const;
    static int GetNextGlobalIndex();

    static std::atomic<uint32_t> globalMeshCount;
	uint32_t m_globalMeshID;

	std::unique_ptr<std::vector<std::byte>> m_vertices;
	std::unique_ptr<std::vector<std::byte>> m_skinningVertices;
	std::vector<meshopt_Meshlet> m_meshlets;
	std::vector<uint32_t> m_meshletVertices;
    std::vector<uint8_t> m_meshletTriangles;
	std::vector<BoundingSphere> m_meshletBounds;

	std::unique_ptr<BufferView> m_postSkinningVertexBufferView = nullptr;
	std::unique_ptr<BufferView> m_preSkinningVertexBufferView = nullptr;

	// TODO: packing
	std::vector<ClusterLODGroup> m_clodGroups;
	std::vector<meshopt_Meshlet> m_clodMeshlets;
	std::vector<uint32_t>        m_clodMeshletVertices;
	std::vector<uint8_t>         m_clodMeshletTriangles;
	std::vector<BoundingSphere>  m_clodMeshletBounds;
	std::vector<int32_t>         m_clodMeshletRefinedGroup;
	//uint32_t                     m_clodRootGroup = 0;
	std::vector<ClusterLODChild> m_clodChildren;
	std::vector<std::byte>       m_clodDuplicatedVertices;
	std::vector<std::byte>       m_clodDuplicatedSkinningVertices;
	std::vector<ClusterLODGroupChunk> m_clodGroupChunks;
	std::vector<std::vector<std::byte>> m_clodGroupVertexChunks;
	std::vector<std::vector<std::byte>> m_clodGroupSkinningVertexChunks;
	std::vector<std::vector<uint32_t>> m_clodGroupMeshletVertexChunks;
	std::vector<std::unique_ptr<BufferView>> m_clodPreSkinningVertexChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodPostSkinningVertexChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodMeshletVertexChunkViews;

	std::vector<ClusterLODNode>      m_clodNodes;
	std::vector<ClusterLODNodeRangeAlloc> m_clodLodNodeRanges;  // per depth
	std::vector<uint32_t>            m_clodLodLevelRoots;        // node index per depth (== 1+depth)
	uint32_t                         m_clodTopRootNode = 0;      // always 0
	uint32_t                         m_clodMaxDepth = 0;

	std::unique_ptr<BufferView> m_clusterLODGroupsView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODChildrenView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODMeshletsView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODMeshletVerticesView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODMeshletTrianglesView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODMeshletBoundsView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODNodesView = nullptr;

	UINT m_indexCount = 0;
    std::shared_ptr<Buffer> m_vertexBufferHandle;
	std::shared_ptr<Buffer> m_indexBufferHandle;
    rhi::VertexBufferView m_vertexBufferView;
    rhi::IndexBufferView m_indexBufferView;

    PerMeshCB m_perMeshBufferData = { 0 };
	unsigned int m_skinningVertexSize = 0;
	std::unique_ptr<BufferView> m_perMeshBufferView;
	MeshManager* m_pCurrentMeshManager = nullptr;

	std::shared_ptr<Skeleton> m_baseSkeleton = nullptr;
};