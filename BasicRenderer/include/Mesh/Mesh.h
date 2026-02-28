#pragma once

#include <directxmath.h>
#include <vector>
#include <memory>
#include <atomic>
#include <optional>
#include <string>
#include <stdexcept>
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
class Mesh;

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

struct ClusterLODDiskChunkSpan
{
	uint64_t offset = 0;
	uint64_t sizeBytes = 0;
};

struct ClusterLODGroupDiskSpans
{
	ClusterLODDiskChunkSpan vertexChunk;
	ClusterLODDiskChunkSpan skinningChunk;
	ClusterLODDiskChunkSpan meshletVertexChunk;
	ClusterLODDiskChunkSpan compressedPositionWordChunk;
	ClusterLODDiskChunkSpan compressedNormalWordChunk;
	ClusterLODDiskChunkSpan compressedMeshletVertexWordChunk;
	ClusterLODDiskChunkSpan meshletChunk;
	ClusterLODDiskChunkSpan meshletTriangleChunk;
	ClusterLODDiskChunkSpan meshletBoundsChunk;
};

struct ClusterLODCacheSource
{
	std::string sourceIdentifier;
	std::string primPath;
	std::string subsetName;
	uint64_t buildConfigHash = 0;
	std::wstring containerFileName;
};

struct ClusterLODPrebuiltData
{
	std::vector<ClusterLODGroup> groups;
	std::vector<ClusterLODChild> children;
	std::vector<std::byte> duplicatedVertices;
	std::vector<std::byte> duplicatedSkinningVertices;
	std::vector<ClusterLODGroupChunk> groupChunks;
	std::vector<std::vector<std::byte>> groupVertexChunks;
	std::vector<std::vector<std::byte>> groupSkinningVertexChunks;
	std::vector<std::vector<uint32_t>> groupMeshletVertexChunks;
	std::vector<std::vector<uint32_t>> groupCompressedPositionWordChunks;
	std::vector<std::vector<uint32_t>> groupCompressedNormalWordChunks;
	std::vector<std::vector<uint32_t>> groupCompressedMeshletVertexWordChunks;
	std::vector<std::vector<meshopt_Meshlet>> groupMeshletChunks;
	std::vector<std::vector<uint8_t>> groupMeshletTriangleChunks;
	std::vector<std::vector<BoundingSphere>> groupMeshletBoundsChunks;
	std::vector<ClusterLODGroupDiskSpans> groupDiskSpans;
	ClusterLODCacheSource cacheSource;
	std::vector<ClusterLODNode> nodes;
};

enum class MeshCpuDataPolicy {
	Retain,
	ReleaseAfterUpload,
};

class Mesh {
public:
	~Mesh()
	{
		auto& deletionManager = DeletionManager::GetInstance();
	}
	static std::shared_ptr<Mesh> CreateShared(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags, std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD = std::nullopt, MeshCpuDataPolicy cpuDataPolicy = MeshCpuDataPolicy::Retain) {
		return std::shared_ptr<Mesh>(new Mesh(std::move(vertices), vertexSize, std::move(skinningVertices), skinningVertexSize, indices, material, flags, std::move(prebuiltClusterLOD), cpuDataPolicy));
    }
	static std::shared_ptr<Mesh> CreateSharedFromIngest(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, std::vector<UINT32>&& indices, const std::shared_ptr<Material> material, unsigned int flags, std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD = std::nullopt, MeshCpuDataPolicy cpuDataPolicy = MeshCpuDataPolicy::Retain) {
		return std::shared_ptr<Mesh>(new Mesh(std::move(vertices), vertexSize, std::move(skinningVertices), skinningVertexSize, indices, material, flags, std::move(prebuiltClusterLOD), cpuDataPolicy));
	}
	uint64_t GetNumVertices(bool meshletReorderedVertices) const {
		//uint64_t size = meshletReorderedVertices ? m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize : m_vertices->size() / m_perMeshBufferData.vertexByteSize;
		return static_cast<uint64_t>(m_perMeshBufferData.numVertices);
	}
	uint64_t GetStreamingNumVertices() const {
		if (!m_clodDuplicatedVertices.empty()) {
			return m_clodDuplicatedVertices.size() / m_perMeshBufferData.vertexByteSize;
		}
		return static_cast<uint64_t>(m_perMeshBufferData.numVertices);
	}
	uint32_t GetCLodGroupCount() const {
		return static_cast<uint32_t>(m_clodGroupChunks.size());
	}
    rhi::VertexBufferView GetVertexBufferView() const;
    rhi::IndexBufferView GetIndexBufferView() const;
	PerMeshCB& GetPerMeshCBData() { return m_perMeshBufferData; };
    UINT GetIndexCount() const;
	uint64_t GetGlobalID() const;
	std::vector<std::byte>& GetVertices() {
		if (!m_vertices) {
			static std::vector<std::byte> empty;
			return empty;
		}
		return *m_vertices;
	}
	const std::vector<std::byte>& GetStreamingVertices() const {
		if (!m_clodDuplicatedVertices.empty()) {
			return m_clodDuplicatedVertices;
		}
		if (m_vertices) {
			return *m_vertices;
		}
		static const std::vector<std::byte> empty;
		return empty;
	}
	//std::vector<std::byte>& GetMeshletReorderedVertices() { return m_meshletReorderedVertices; }
	std::vector<std::byte>& GetSkinningVertices() {
		if (!m_skinningVertices) {
			static std::vector<std::byte> empty;
			return empty;
		}
		return *m_skinningVertices;
	}
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
		return m_perMeshBufferData.clodNumMeshlets;
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
		std::unique_ptr<BufferView> clodNodesView
	);

	const std::vector<ClusterLODGroup>& GetCLodGroups() const {
		return m_clodGroups;
	}

	const std::vector<ClusterLODChild>& GetCLodChildren() const {
		return m_clodChildren;
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

	const std::vector<std::vector<uint32_t>>& GetCLodGroupCompressedPositionWordChunks() const {
		return m_clodGroupCompressedPositionWordChunks;
	}

	const std::vector<std::vector<uint32_t>>& GetCLodGroupCompressedNormalWordChunks() const {
		return m_clodGroupCompressedNormalWordChunks;
	}

	const std::vector<std::vector<uint32_t>>& GetCLodGroupCompressedMeshletVertexWordChunks() const {
		return m_clodGroupCompressedMeshletVertexWordChunks;
	}

	const std::vector<std::vector<meshopt_Meshlet>>& GetCLodGroupMeshletChunks() const {
		return m_clodGroupMeshletChunks;
	}

	const std::vector<std::vector<uint8_t>>& GetCLodGroupMeshletTriangleChunks() const {
		return m_clodGroupMeshletTriangleChunks;
	}

	const std::vector<std::vector<BoundingSphere>>& GetCLodGroupMeshletBoundsChunks() const {
		return m_clodGroupMeshletBoundsChunks;
	}

	const std::vector<ClusterLODGroupDiskSpans>& GetCLodGroupDiskSpans() const {
		return m_clodGroupDiskSpans;
	}

	const ClusterLODCacheSource& GetCLodCacheSource() const {
		return m_clodCacheSource;
	}

	bool HasCLodDiskStreamingSource() const {
		return !m_clodGroupDiskSpans.empty() && !m_clodCacheSource.containerFileName.empty();
	}

	void ReleaseCLodChunkUploadData() {
		m_clodGroupVertexChunks.clear();
		m_clodGroupVertexChunks.shrink_to_fit();
		m_clodGroupSkinningVertexChunks.clear();
		m_clodGroupSkinningVertexChunks.shrink_to_fit();
		m_clodGroupMeshletVertexChunks.clear();
		m_clodGroupMeshletVertexChunks.shrink_to_fit();
		m_clodGroupCompressedPositionWordChunks.clear();
		m_clodGroupCompressedPositionWordChunks.shrink_to_fit();
		m_clodGroupCompressedNormalWordChunks.clear();
		m_clodGroupCompressedNormalWordChunks.shrink_to_fit();
		m_clodGroupCompressedMeshletVertexWordChunks.clear();
		m_clodGroupCompressedMeshletVertexWordChunks.shrink_to_fit();
		m_clodGroupMeshletChunks.clear();
		m_clodGroupMeshletChunks.shrink_to_fit();
		m_clodGroupMeshletTriangleChunks.clear();
		m_clodGroupMeshletTriangleChunks.shrink_to_fit();
		m_clodGroupMeshletBoundsChunks.clear();
		m_clodGroupMeshletBoundsChunks.shrink_to_fit();
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

	const BufferView* GetCLodNodesView() const {
		return m_clusterLODNodesView.get();
	}

	void SetCLodGroupChunkViews(
		std::vector<std::unique_ptr<BufferView>> preSkinningVertexChunkViews,
		std::vector<std::unique_ptr<BufferView>> postSkinningVertexChunkViews,
		std::vector<std::unique_ptr<BufferView>> meshletVertexChunkViews,
		std::vector<std::unique_ptr<BufferView>> compressedPositionChunkViews,
		std::vector<std::unique_ptr<BufferView>> compressedNormalChunkViews,
		std::vector<std::unique_ptr<BufferView>> compressedMeshletVertexChunkViews,
		std::vector<std::unique_ptr<BufferView>> meshletChunkViews,
		std::vector<std::unique_ptr<BufferView>> meshletTriangleChunkViews,
		std::vector<std::unique_ptr<BufferView>> meshletBoundsChunkViews) {
		m_clodPreSkinningVertexChunkViews = std::move(preSkinningVertexChunkViews);
		m_clodPostSkinningVertexChunkViews = std::move(postSkinningVertexChunkViews);
		m_clodMeshletVertexChunkViews = std::move(meshletVertexChunkViews);
		m_clodCompressedPositionChunkViews = std::move(compressedPositionChunkViews);
		m_clodCompressedNormalChunkViews = std::move(compressedNormalChunkViews);
		m_clodCompressedMeshletVertexChunkViews = std::move(compressedMeshletVertexChunkViews);
		m_clodMeshletChunkViews = std::move(meshletChunkViews);
		m_clodMeshletTriangleChunkViews = std::move(meshletTriangleChunkViews);
		m_clodMeshletBoundsChunkViews = std::move(meshletBoundsChunkViews);
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

	const std::vector<std::unique_ptr<BufferView>>& GetCLodCompressedPositionChunkViews() const {
		return m_clodCompressedPositionChunkViews;
	}

	const std::vector<std::unique_ptr<BufferView>>& GetCLodCompressedNormalChunkViews() const {
		return m_clodCompressedNormalChunkViews;
	}

	const std::vector<std::unique_ptr<BufferView>>& GetCLodCompressedMeshletVertexChunkViews() const {
		return m_clodCompressedMeshletVertexChunkViews;
	}

	const std::vector<std::unique_ptr<BufferView>>& GetCLodMeshletChunkViews() const {
		return m_clodMeshletChunkViews;
	}

	const std::vector<std::unique_ptr<BufferView>>& GetCLodMeshletTriangleChunkViews() const {
		return m_clodMeshletTriangleChunkViews;
	}

	const std::vector<std::unique_ptr<BufferView>>& GetCLodMeshletBoundsChunkViews() const {
		return m_clodMeshletBoundsChunkViews;
	}

	std::vector<std::unique_ptr<BufferView>>& AccessCLodPostSkinningVertexChunkViews() {
		return m_clodPostSkinningVertexChunkViews;
	}

	std::vector<std::unique_ptr<BufferView>>& AccessCLodMeshletVertexChunkViews() {
		return m_clodMeshletVertexChunkViews;
	}

	std::vector<std::unique_ptr<BufferView>>& AccessCLodCompressedPositionChunkViews() {
		return m_clodCompressedPositionChunkViews;
	}

	std::vector<std::unique_ptr<BufferView>>& AccessCLodCompressedNormalChunkViews() {
		return m_clodCompressedNormalChunkViews;
	}

	std::vector<std::unique_ptr<BufferView>>& AccessCLodCompressedMeshletVertexChunkViews() {
		return m_clodCompressedMeshletVertexChunkViews;
	}

	std::vector<std::unique_ptr<BufferView>>& AccessCLodMeshletChunkViews() {
		return m_clodMeshletChunkViews;
	}

	std::vector<std::unique_ptr<BufferView>>& AccessCLodMeshletTriangleChunkViews() {
		return m_clodMeshletTriangleChunkViews;
	}

	std::vector<std::unique_ptr<BufferView>>& AccessCLodMeshletBoundsChunkViews() {
		return m_clodMeshletBoundsChunkViews;
	}

	uint32_t GetCLodRootNodeIndex() const { // For hierarchy cut
		return m_clodTopRootNode;
	}

	uint32_t GetCoarsestLODRootNodeIndex() const { // For DAG traversal
		return m_clodLodLevelRoots.back();
	}

	ClusterLODPrebuiltData GetClusterLODPrebuiltData() const;

private:
	Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material>, unsigned int flags, std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD, MeshCpuDataPolicy cpuDataPolicy);
	void ReleaseCpuGeometryData();
    void CreateVertexBuffer();
    void CreateMeshlets(const std::vector<UINT32>& indices);
	//void CreateMeshletReorderedVertices();
    void CreateBuffers(const std::vector<UINT32>& indices);
	void ComputeBoundingSphere(const std::vector<UINT32>& indices);
	void ComputeAABB(DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max);
	void BuildClusterLOD(const std::vector<UINT32>& indices);
	void BuildClusterLODTraversalHierarchy(uint32_t preferredNodeWidth);
	void LogClusterLODHierarchyStats() const;
	void ApplyPrebuiltClusterLODData(const ClusterLODPrebuiltData& data);
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
	//uint32_t                     m_clodRootGroup = 0;
	std::vector<ClusterLODChild> m_clodChildren;
	std::vector<std::byte>       m_clodDuplicatedVertices;
	std::vector<std::byte>       m_clodDuplicatedSkinningVertices;
	std::vector<ClusterLODGroupChunk> m_clodGroupChunks;
	std::vector<std::vector<std::byte>> m_clodGroupVertexChunks;
	std::vector<std::vector<std::byte>> m_clodGroupSkinningVertexChunks;
	std::vector<std::vector<uint32_t>> m_clodGroupMeshletVertexChunks;
	std::vector<std::vector<uint32_t>> m_clodGroupCompressedPositionWordChunks;
	std::vector<std::vector<uint32_t>> m_clodGroupCompressedNormalWordChunks;
	std::vector<std::vector<uint32_t>> m_clodGroupCompressedMeshletVertexWordChunks;
	std::vector<std::vector<meshopt_Meshlet>> m_clodGroupMeshletChunks;
	std::vector<std::vector<uint8_t>> m_clodGroupMeshletTriangleChunks;
	std::vector<std::vector<BoundingSphere>> m_clodGroupMeshletBoundsChunks;
	std::vector<ClusterLODGroupDiskSpans> m_clodGroupDiskSpans;
	ClusterLODCacheSource m_clodCacheSource;
	std::vector<std::unique_ptr<BufferView>> m_clodPreSkinningVertexChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodPostSkinningVertexChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodMeshletVertexChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodCompressedPositionChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodCompressedNormalChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodCompressedMeshletVertexChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodMeshletChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodMeshletTriangleChunkViews;
	std::vector<std::unique_ptr<BufferView>> m_clodMeshletBoundsChunkViews;

	std::vector<ClusterLODNode>      m_clodNodes;
	std::vector<ClusterLODNodeRangeAlloc> m_clodLodNodeRanges;  // per depth
	std::vector<uint32_t>            m_clodLodLevelRoots;        // node index per depth (== 1+depth)
	uint32_t                         m_clodTopRootNode = 0;      // always 0
	uint32_t                         m_clodMaxDepth = 0;
	std::optional<ClusterLODPrebuiltData> m_prebuiltClusterLOD;

	std::unique_ptr<BufferView> m_clusterLODGroupsView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODChildrenView = nullptr;
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

class MeshIngestBuilder {
public:
	MeshIngestBuilder(unsigned int vertexSize, unsigned int skinningVertexSize, unsigned int flags)
		: m_vertexSize(vertexSize), m_skinningVertexSize(skinningVertexSize), m_flags(flags) {}

	void ReserveVertices(size_t vertexCount) {
		m_vertices.reserve(vertexCount * static_cast<size_t>(m_vertexSize));
	}

	void ReserveIndices(size_t indexCount) {
		m_indices.reserve(indexCount);
	}

	void AppendVertexBytes(const std::byte* data, size_t byteCount) {
		if (byteCount != m_vertexSize) {
			throw std::runtime_error("MeshIngestBuilder vertex byte size mismatch");
		}
		m_vertices.insert(m_vertices.end(), data, data + byteCount);
	}

	void AppendSkinningVertexBytes(const std::byte* data, size_t byteCount) {
		if (m_skinningVertexSize == 0) {
			throw std::runtime_error("MeshIngestBuilder has no skinning vertex format");
		}
		if (byteCount != m_skinningVertexSize) {
			throw std::runtime_error("MeshIngestBuilder skinning vertex byte size mismatch");
		}
		m_skinningVertices.insert(m_skinningVertices.end(), data, data + byteCount);
	}

	void AppendIndex(uint32_t index) {
		m_indices.push_back(index);
	}

	void AppendIndices(const uint32_t* data, size_t count) {
		m_indices.insert(m_indices.end(), data, data + count);
	}

	std::shared_ptr<Mesh> Build(
		const std::shared_ptr<Material>& material,
		std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD = std::nullopt,
		MeshCpuDataPolicy cpuDataPolicy = MeshCpuDataPolicy::Retain);

private:
	unsigned int m_vertexSize = 0;
	unsigned int m_skinningVertexSize = 0;
	unsigned int m_flags = 0;
	std::vector<std::byte> m_vertices;
	std::vector<std::byte> m_skinningVertices;
	std::vector<uint32_t> m_indices;
};