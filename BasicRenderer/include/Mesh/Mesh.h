#pragma once

#include <directxmath.h>
#include <vector>
#include <memory>
#include <atomic>
#include <optional>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <rhi.h>
#include <meshoptimizer.h>

#include "Mesh/ClusterLODTypes.h"
#include "Import/MeshData.h"
#include "ShaderBuffers.h"
#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/DeletionManager.h"

class MeshManager;
class Skeleton;
class Buffer;

class Mesh {
public:
	using SparseChunkViewTable = std::unordered_map<uint32_t, std::unique_ptr<BufferView>>;

	~Mesh()
	{
		auto& deletionManager = DeletionManager::GetInstance();
	}
	static std::shared_ptr<Mesh> CreateShared(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, std::vector<MeshUvSetData>&& uvSets, const std::shared_ptr<Material> material, unsigned int flags, std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD = std::nullopt, MeshCpuDataPolicy cpuDataPolicy = MeshCpuDataPolicy::Retain) {
		return std::shared_ptr<Mesh>(new Mesh(std::move(vertices), vertexSize, std::move(skinningVertices), skinningVertexSize, indices, std::move(uvSets), material, flags, std::move(prebuiltClusterLOD), cpuDataPolicy));
    }
	static std::shared_ptr<Mesh> CreateSharedFromIngest(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, std::vector<UINT32>&& indices, std::vector<MeshUvSetData>&& uvSets, const std::shared_ptr<Material> material, unsigned int flags, std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD = std::nullopt, MeshCpuDataPolicy cpuDataPolicy = MeshCpuDataPolicy::Retain) {
		return std::shared_ptr<Mesh>(new Mesh(std::move(vertices), vertexSize, std::move(skinningVertices), skinningVertexSize, indices, std::move(uvSets), material, flags, std::move(prebuiltClusterLOD), cpuDataPolicy));
	}
	uint64_t GetNumVertices(bool meshletReorderedVertices) const {
		//uint64_t size = meshletReorderedVertices ? m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize : m_vertices->size() / m_perMeshBufferData.vertexByteSize;
		return static_cast<uint64_t>(m_perMeshBufferData.numVertices);
	}

	uint32_t GetCLodGroupCount() const {
		return static_cast<uint32_t>(m_clodRuntimeSummary.groupChunkHints.size());
	}

    bool IsCLodMesh() const {
        return GetCLodGroupCount() > 0u;
    }

	PerMeshCB& GetPerMeshCBData() { return m_perMeshBufferData; };
	uint64_t GetGlobalID() const;

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

	void SetMaterialDataIndex(unsigned int index);

	void SetCLodBufferViews(
		std::unique_ptr<BufferView> clusterLODGroupsView,
		std::unique_ptr<BufferView> clusterLODSegmentsView,
		std::unique_ptr<BufferView> clodNodesView
	);

	const std::vector<ClusterLODGroup>& GetCLodGroups() const {
		return m_clodGroups;
	}

	const std::vector<ClusterLODGroupSegment>& GetCLodSegments() const {
		return m_clodSegments;
	}

	const ClusterLODRuntimeSummary& GetCLodRuntimeSummary() const {
		return m_clodRuntimeSummary;
	}

	const std::vector<ClusterLODRuntimeSummary::GroupChunkHint>& GetCLodGroupChunkHints() const {
		return m_clodRuntimeSummary.groupChunkHints;
	}

	const std::vector<ClusterLODGroupDiskLocator>& GetCLodGroupDiskLocators() const {
		return m_clodGroupDiskLocators;
	}

	const ClusterLODCacheSource& GetCLodCacheSource() const {
		return m_clodCacheSource;
	}

	bool HasCLodDiskStreamingSource() const {
		return !m_clodCacheSource.containerFileName.empty();
	}

	void AdoptCLodDiskStreamingMetadata(const ClusterLODPrebuiltData& data);

	void ReleaseCLodChunkUploadData();
	void ReleaseCLodHierarchyCpuData();
	void ReleaseCLodGroupChunkMetadataCpuData();

	const std::vector<ClusterLODNode>& GetCLodNodes() const {
		return m_clodNodes;
	}

	const BufferView* GetCLodGroupsView() const {
		return m_clusterLODGroupsView.get();
	}

	const BufferView* GetCLodSegmentsView() const {
		return m_clusterLODSegmentsView.get();
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
		m_clodPreSkinningVertexChunkViews = ToSparseChunkViewTable(std::move(preSkinningVertexChunkViews));
		m_clodPostSkinningVertexChunkViews = ToSparseChunkViewTable(std::move(postSkinningVertexChunkViews));
		m_clodMeshletVertexChunkViews = ToSparseChunkViewTable(std::move(meshletVertexChunkViews));
		m_clodCompressedPositionChunkViews = ToSparseChunkViewTable(std::move(compressedPositionChunkViews));
		m_clodCompressedNormalChunkViews = ToSparseChunkViewTable(std::move(compressedNormalChunkViews));
		m_clodCompressedMeshletVertexChunkViews = ToSparseChunkViewTable(std::move(compressedMeshletVertexChunkViews));
		m_clodMeshletChunkViews = ToSparseChunkViewTable(std::move(meshletChunkViews));
		m_clodMeshletTriangleChunkViews = ToSparseChunkViewTable(std::move(meshletTriangleChunkViews));
		m_clodMeshletBoundsChunkViews = ToSparseChunkViewTable(std::move(meshletBoundsChunkViews));
	}

	const SparseChunkViewTable& GetCLodPreSkinningVertexChunkViews() const {
		return m_clodPreSkinningVertexChunkViews;
	}

	const SparseChunkViewTable& GetCLodPostSkinningVertexChunkViews() const {
		return m_clodPostSkinningVertexChunkViews;
	}

	const SparseChunkViewTable& GetCLodMeshletVertexChunkViews() const {
		return m_clodMeshletVertexChunkViews;
	}

	const SparseChunkViewTable& GetCLodCompressedPositionChunkViews() const {
		return m_clodCompressedPositionChunkViews;
	}

	const SparseChunkViewTable& GetCLodCompressedNormalChunkViews() const {
		return m_clodCompressedNormalChunkViews;
	}

	const SparseChunkViewTable& GetCLodCompressedMeshletVertexChunkViews() const {
		return m_clodCompressedMeshletVertexChunkViews;
	}

	const SparseChunkViewTable& GetCLodMeshletChunkViews() const {
		return m_clodMeshletChunkViews;
	}

	const SparseChunkViewTable& GetCLodMeshletTriangleChunkViews() const {
		return m_clodMeshletTriangleChunkViews;
	}

	const SparseChunkViewTable& GetCLodMeshletBoundsChunkViews() const {
		return m_clodMeshletBoundsChunkViews;
	}

	const BufferView* GetCLodPostSkinningVertexChunkView(uint32_t groupIndex) const {
		return GetChunkViewAt(m_clodPostSkinningVertexChunkViews, groupIndex);
	}

	const BufferView* GetCLodMeshletVertexChunkView(uint32_t groupIndex) const {
		return GetChunkViewAt(m_clodMeshletVertexChunkViews, groupIndex);
	}

	const BufferView* GetCLodCompressedPositionChunkView(uint32_t groupIndex) const {
		return GetChunkViewAt(m_clodCompressedPositionChunkViews, groupIndex);
	}

	const BufferView* GetCLodCompressedNormalChunkView(uint32_t groupIndex) const {
		return GetChunkViewAt(m_clodCompressedNormalChunkViews, groupIndex);
	}

	const BufferView* GetCLodCompressedMeshletVertexChunkView(uint32_t groupIndex) const {
		return GetChunkViewAt(m_clodCompressedMeshletVertexChunkViews, groupIndex);
	}

	const BufferView* GetCLodMeshletChunkView(uint32_t groupIndex) const {
		return GetChunkViewAt(m_clodMeshletChunkViews, groupIndex);
	}

	const BufferView* GetCLodMeshletTriangleChunkView(uint32_t groupIndex) const {
		return GetChunkViewAt(m_clodMeshletTriangleChunkViews, groupIndex);
	}

	const BufferView* GetCLodMeshletBoundsChunkView(uint32_t groupIndex) const {
		return GetChunkViewAt(m_clodMeshletBoundsChunkViews, groupIndex);
	}

	void SetCLodPostSkinningVertexChunkView(uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		SetChunkViewAt(m_clodPostSkinningVertexChunkViews, groupIndex, std::move(view));
	}

	void SetCLodMeshletVertexChunkView(uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		SetChunkViewAt(m_clodMeshletVertexChunkViews, groupIndex, std::move(view));
	}

	void SetCLodCompressedPositionChunkView(uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		SetChunkViewAt(m_clodCompressedPositionChunkViews, groupIndex, std::move(view));
	}

	void SetCLodCompressedNormalChunkView(uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		SetChunkViewAt(m_clodCompressedNormalChunkViews, groupIndex, std::move(view));
	}

	void SetCLodCompressedMeshletVertexChunkView(uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		SetChunkViewAt(m_clodCompressedMeshletVertexChunkViews, groupIndex, std::move(view));
	}

	void SetCLodMeshletChunkView(uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		SetChunkViewAt(m_clodMeshletChunkViews, groupIndex, std::move(view));
	}

	void SetCLodMeshletTriangleChunkView(uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		SetChunkViewAt(m_clodMeshletTriangleChunkViews, groupIndex, std::move(view));
	}

	void SetCLodMeshletBoundsChunkView(uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		SetChunkViewAt(m_clodMeshletBoundsChunkViews, groupIndex, std::move(view));
	}

	uint32_t GetCLodRootNodeIndex() const { // For hierarchy cut
		return m_clodTopRootNode;
	}

	uint32_t GetCoarsestLODRootNodeIndex() const { // For DAG traversal
		return m_clodLodLevelRoots.back();
	}

	const VoxelGroupMapping& GetVoxelGroupMapping() const {
		return m_voxelGroupMapping;
	}

	const std::vector<MeshUvSetData>& GetUvSets() const {
		return m_uvSets;
	}

	bool HasVoxelGroups() const {
		return !m_voxelGroupMapping.payloads.empty();
	}

	ClusterLODPrebuiltData GetClusterLODPrebuiltData() const;
	ClusterLODCacheBuildPayload GetClusterLODCacheBuildPayload() const;
	ClusterLODCacheBuildOwnedData GetClusterLODCacheBuildOwnedData() const;

private:
	Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, std::vector<MeshUvSetData>&& uvSets, const std::shared_ptr<Material>, unsigned int flags, std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD, MeshCpuDataPolicy cpuDataPolicy, bool deferResourceCreation = false);
    void CreateBuffers(const std::vector<UINT32>& indices);
	void ApplyPrebuiltClusterLODData(const ClusterLODPrebuiltData& data);
	void ClearCLodCacheBuildChunkData(bool shrinkToFit);
	static SparseChunkViewTable ToSparseChunkViewTable(std::vector<std::unique_ptr<BufferView>>&& denseViews) {
		SparseChunkViewTable sparseViews;
		sparseViews.reserve(denseViews.size());
		for (uint32_t i = 0; i < denseViews.size(); ++i) {
			if (denseViews[i] != nullptr) {
				sparseViews.emplace(i, std::move(denseViews[i]));
			}
		}
		return sparseViews;
	}
	static const BufferView* GetChunkViewAt(const SparseChunkViewTable& views, uint32_t groupIndex) {
		auto it = views.find(groupIndex);
		return (it != views.end() && it->second != nullptr) ? it->second.get() : nullptr;
	}
	static void SetChunkViewAt(SparseChunkViewTable& views, uint32_t groupIndex, std::unique_ptr<BufferView> view) {
		if (view == nullptr) {
			views.erase(groupIndex);
			return;
		}
		views[groupIndex] = std::move(view);
	}
    static int GetNextGlobalIndex();

    static std::atomic<uint32_t> globalMeshCount;
	uint32_t m_globalMeshID;

	std::unique_ptr<BufferView> m_postSkinningVertexBufferView = nullptr;
	std::unique_ptr<BufferView> m_preSkinningVertexBufferView = nullptr;

	// TODO: packing
	std::vector<ClusterLODGroup> m_clodGroups;
	std::vector<ClusterLODGroupSegment> m_clodSegments;
	std::vector<ClusterLODGroupChunk> m_clodGroupChunks;
	ClusterLODRuntimeSummary m_clodRuntimeSummary;
	struct ClusterLODCacheBuildChunkData {
		std::vector<std::vector<std::vector<std::byte>>> groupPageBlobs;
	} m_clodCacheBuildChunkData;
	std::vector<ClusterLODGroupDiskLocator> m_clodGroupDiskLocators;
	ClusterLODCacheSource m_clodCacheSource;
	SparseChunkViewTable m_clodPreSkinningVertexChunkViews;
	SparseChunkViewTable m_clodPostSkinningVertexChunkViews;
	SparseChunkViewTable m_clodMeshletVertexChunkViews;
	SparseChunkViewTable m_clodCompressedPositionChunkViews;
	SparseChunkViewTable m_clodCompressedNormalChunkViews;
	SparseChunkViewTable m_clodCompressedMeshletVertexChunkViews;
	SparseChunkViewTable m_clodMeshletChunkViews;
	SparseChunkViewTable m_clodMeshletTriangleChunkViews;
	SparseChunkViewTable m_clodMeshletBoundsChunkViews;

	std::vector<ClusterLODNode>      m_clodNodes;
	std::vector<ClusterLODNodeRangeAlloc> m_clodLodNodeRanges;  // per depth
	std::vector<uint32_t>            m_clodLodLevelRoots;        // node index per depth (== 1+depth)
	uint32_t                         m_clodTopRootNode = 0;      // always 0
	uint32_t                         m_clodMaxDepth = 0;
	VoxelGroupMapping                m_voxelGroupMapping;        // Per-group voxel payloads (empty when no voxel groups exist)
	std::optional<ClusterLODPrebuiltData> m_prebuiltClusterLOD;

	std::unique_ptr<BufferView> m_clusterLODGroupsView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODSegmentsView = nullptr;
	std::unique_ptr<BufferView> m_clusterLODNodesView = nullptr;

	//UINT m_indexCount = 0;
    //std::shared_ptr<Buffer> m_vertexBufferHandle;
	//std::shared_ptr<Buffer> m_indexBufferHandle;
    //rhi::VertexBufferView m_vertexBufferView;
    //rhi::IndexBufferView m_indexBufferView;

    PerMeshCB m_perMeshBufferData = { 0 };
	unsigned int m_skinningVertexSize = 0;
	std::vector<MeshUvSetData> m_uvSets;
	std::unique_ptr<BufferView> m_perMeshBufferView;
	MeshManager* m_pCurrentMeshManager = nullptr;

	std::shared_ptr<Skeleton> m_baseSkeleton = nullptr;
};
