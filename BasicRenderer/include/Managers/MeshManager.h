#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ShaderBuffers.h"
#include "Mesh/Mesh.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/Buffers/PagePool.h"
#include "Interfaces/IResourceProvider.h"

class Mesh;
class MeshInstance;
class DynamicBuffer;
class ResourceGroup;
class BufferView;
class ViewManager;

// TODO: Find better way of batching these with namespaces
#define MESH_RESOURCE_IDFENTIFIERS Builtin::MeshResources::MeshletOffsets, Builtin::MeshResources::MeshletVertexIndices, Builtin::MeshResources::MeshletTriangles

class MeshManager : public IResourceProvider {
public:
	struct CLodActiveGroupRange {
		uint32_t groupsBase = 0;
		uint32_t groupCount = 0;
	};

	struct CLodGlobalResidencyRequest {
		uint32_t groupGlobalIndex = 0;
		bool resident = false;
	};

	struct CLodGlobalResidencyResult {
		bool serviced = false;
		bool applied = false;
		bool queued = false;
	};

	struct CLodStreamingDebugStats {
		uint32_t residentGroups = 0;
		uint32_t residentAllocations = 0;
		uint32_t queuedRequests = 0;
		uint32_t completedResults = 0;
		uint64_t residentAllocationBytes = 0;
		uint64_t completedResultBytes = 0;
	};

	// Represents the outcome of a single disk-streamed group IO.
	struct CLodDiskStreamingCompletion {
		uint32_t groupGlobalIndex = 0;
		bool success = false;
	};

	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	~MeshManager();
	void AddMesh(std::shared_ptr<Mesh>& mesh, bool useMeshletReorderedVertices);
	void AddMeshInstance(MeshInstance* mesh, bool useMeshletReorderedVertices);
	void RemoveMesh(Mesh* mesh);
	void RemoveMeshInstance(MeshInstance* mesh);

	uint32_t SetCLodGroupResidencyForGlobal(uint32_t groupGlobalIndex, bool resident);
	void SetCLodGroupResidencyForGlobalBatch(
		const std::vector<CLodGlobalResidencyRequest>& requests,
		std::vector<uint32_t>& outAppliedCounts);
	CLodGlobalResidencyResult SetCLodGroupResidencyForGlobalEx(uint32_t groupGlobalIndex, bool resident);
	void SetCLodGroupResidencyForGlobalBatchEx(
		const std::vector<CLodGlobalResidencyRequest>& requests,
		std::vector<CLodGlobalResidencyResult>& outResults);
	void GetCLodActiveUniqueAssetGroupRanges(std::vector<CLodActiveGroupRange>& outRanges, uint32_t& outMaxGroupIndex) const;
	void GetCLodCoarsestUniqueAssetGroupRanges(std::vector<CLodActiveGroupRange>& outRanges) const;
	void GetCLodUniqueAssetParentMap(std::vector<int32_t>& outParentGroupByGlobal, uint32_t& outMaxGroupIndex) const;

	// Fused single-pass snapshot that combines GetCLodActiveUniqueAssetGroupRanges,
	// GetCLodCoarsestUniqueAssetGroupRanges, and GetCLodUniqueAssetParentMap.
	struct CLodStreamingDomainSnapshot {
		std::vector<CLodActiveGroupRange> activeRanges;
		std::vector<CLodActiveGroupRange> coarsestRanges;
		std::vector<int32_t> parentGroupByGlobal;
		uint32_t maxGroupIndex = 0;
	};
	void GetCLodStreamingDomainSnapshot(CLodStreamingDomainSnapshot& outSnapshot) const;

	// Returns true when mesh/instance adds or removes have changed the
	// streaming structure since the last call.  After returning true the
	// flag is cleared automatically so subsequent calls return false until
	// the next structural change.
	bool ConsumeCLodStreamingStructureDirty();

	CLodStreamingDebugStats GetCLodStreamingDebugStats() const;
	void ProcessCLodDiskStreamingIO(uint32_t maxCompletedRequests = 64u);

	// Drains groups that completed disk streaming since the last call.
	// The extension uses this to learn which groups became resident (or failed)
	// so it can update the GPU-visible non-resident bitset accordingly.
	void DrainCompletedCLodDiskStreamingGroups(std::vector<CLodDiskStreamingCompletion>& outCompletions);

	void UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data);
	void UpdatePerMeshInstanceBuffer(std::unique_ptr<BufferView>& view, PerMeshInstanceCB& data);
	void SetViewManager(ViewManager* viewManager) { m_pViewManager = viewManager; }

	// Access the CLod page pool (may be null if no CLod meshes loaded).
	PagePool* GetCLodPagePool() const { return m_clodPagePool.get(); }
	uint64_t GetActiveMeshletCount() const { return m_activeMeshletCount; }

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
	MeshManager();
	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::shared_ptr<DynamicBuffer> m_preSkinningVertices; // Used for skinned meshes
	std::shared_ptr<DynamicBuffer> m_postSkinningVertices; // Used by all meshes
	std::shared_ptr<DynamicBuffer> m_meshletOffsets; // meshopt_Meshlet
	std::shared_ptr<DynamicBuffer> m_meshletVertexIndices; // 
	std::shared_ptr<DynamicBuffer> m_meshletTriangles;

	//std::shared_ptr<DynamicBuffer> m_meshletBoundsBuffer;
	//std::shared_ptr<DynamicBuffer> m_meshletBitfieldBuffer;
	//std::shared_ptr<DynamicBuffer> m_clusterToVisibleClusterTableIndexBuffer; // Used by visibility buffer, for drawcall indexing

	// Base meshes
	std::shared_ptr<DynamicBuffer> m_perMeshBuffers;

	// mesh instances
	std::shared_ptr<DynamicBuffer> m_perMeshInstanceBuffers;

	std::shared_ptr<DynamicBuffer> m_perMeshInstanceClodOffsets;
	std::shared_ptr<DynamicBuffer> m_clodSharedGroupChunks;
	std::shared_ptr<DynamicBuffer> m_clodMeshMetadata;
	std::shared_ptr<DynamicBuffer> m_clusterLODGroups;
	std::shared_ptr<DynamicBuffer> m_clusterLODChildren;

	//std::shared_ptr<DynamicBuffer> m_clusterLODMeshlets;
	std::shared_ptr<DynamicBuffer> m_clusterLODMeshletBounds;
	std::shared_ptr<DynamicBuffer> m_clusterLODNodes;
	uint64_t m_activeMeshletCount = 0;

	struct CLodSharedStreamingState {
		struct ResidentGroupAllocations {
			// Page-pool allocation (all 8 data streams live here)
			PagePool::PageAllocation pageAllocation;

			void Reset() {
				pageAllocation.Reset();
			}
		};

		Mesh* mesh = nullptr;
		std::unique_ptr<BufferView> ownedMeshMetadataView;
		uint32_t clodMeshMetadataIndex = 0;
		uint32_t groupsBase = 0;
		uint32_t groupCount = 0;
		std::unique_ptr<BufferView> ownedGroupChunksView;
		BufferView* groupChunksView = nullptr;
		std::vector<ClusterLODGroupChunk> baselineGroupChunks;
		std::vector<uint8_t> groupResidentFlags;
		std::vector<ResidentGroupAllocations> residentGroupAllocations;
		uint32_t activeInstanceCount = 0;
		bool residencyTableDirty = false;
	};

	struct CLodSharedStreamingRange {
		uint32_t begin = 0;
		uint32_t end = 0;
		std::shared_ptr<CLodSharedStreamingState> state;
	};

	struct CLodStreamingInstanceState {
		MeshInstance* instance = nullptr;
		uint32_t meshInstanceIndex = 0;
		uint32_t groupsBase = 0;
		uint32_t groupCount = 0;
		std::shared_ptr<CLodSharedStreamingState> sharedMeshState;
	};

	std::unordered_map<uint32_t, CLodStreamingInstanceState> m_clodStreamingStateByInstanceIndex;
	std::unordered_map<const MeshInstance*, uint32_t> m_clodStreamingInstanceIndexByPtr;
	std::unordered_map<const Mesh*, std::shared_ptr<CLodSharedStreamingState>> m_clodSharedStreamingStateByMesh;
	std::vector<CLodSharedStreamingRange> m_clodSharedStreamingRanges;
	bool m_clodSharedStreamingRangesDirty = true;
	// Set whenever mesh/instance structural changes occur; consumed by CLodExtension.
	std::atomic<bool> m_clodStreamingStructureDirty{true};

	// Incremental debug-stats counters — updated in place by residency mutations.
	std::atomic<uint32_t> m_debugResidentGroups{0};
	std::atomic<uint32_t> m_debugResidentAllocations{0};
	std::atomic<uint64_t> m_debugResidentAllocationBytes{0};

	struct CLodDiskStreamingRequest {
		uint32_t groupGlobalIndex = 0;
		ClusterLODCacheSource cacheSource{};
		uint32_t groupLocalIndex = 0;
	};

	struct CLodDiskStreamingResult {
		uint32_t groupGlobalIndex = 0;
		bool success = false;
		std::optional<ClusterLODGroupChunk> groupChunkMetadata;
		std::vector<std::byte> vertexChunk;
		std::vector<uint32_t> meshletVertexChunk;
		std::vector<uint32_t> compressedPositionWordChunk;
		std::vector<uint32_t> compressedNormalWordChunk;
		std::vector<uint32_t> compressedMeshletVertexWordChunk;
		std::vector<meshopt_Meshlet> meshletChunk;
		std::vector<uint8_t> meshletTriangleChunk;
		std::vector<BoundingSphere> meshletBoundsChunk;
	};

	// Pending requests waiting to be dispatched (guarded by m_clodDiskStreamingMutex).
	mutable std::mutex m_clodDiskStreamingMutex;
	std::vector<CLodDiskStreamingRequest> m_clodDiskStreamingRequests;
	std::unordered_set<uint32_t> m_clodDiskStreamingQueuedGroups;

	// Guards m_clodDiskStreamingResults and m_clodDiskStreamingCompletions.
	mutable std::mutex m_clodDiskStreamingResultsMutex;

	// Completed results waiting to be applied on the main thread.
	std::vector<CLodDiskStreamingResult> m_clodDiskStreamingResults;
	std::vector<CLodDiskStreamingCompletion> m_clodDiskStreamingCompletions;

	// Guards CLodSharedStreamingState interiors (groupResidentFlags,
	// baselineGroupChunks, residentGroupAllocations, residencyTableDirty),
	// m_clodPagePool, and m_clodSharedGroupChunks UpdateView calls.
	mutable std::mutex m_clodResidencyMutex;

	// Maximum number of IO requests dispatched per ProcessCLodDiskStreamingIO call.
	static constexpr uint32_t kMaxIoBatchSize = 128u;

	void DispatchCLodDiskStreamingBatch();
	bool QueueCLodDiskStreamingRequest(uint32_t groupGlobalIndex, CLodSharedStreamingState& state, uint32_t groupLocalIndex, bool& outQueued);
	bool ApplyCompletedCLodDiskStreamingResult(CLodDiskStreamingResult& result);
	void UploadCLodGroupChunkTable(const CLodSharedStreamingState& state);
	bool IsCLodGroupResident(const CLodSharedStreamingState& state, uint32_t groupLocalIndex) const;
	void DeallocateCLodGroupChunkAllocations(CLodSharedStreamingState& state, uint32_t groupLocalIndex);
	void ReleaseAllCLodGroupChunkAllocations(CLodSharedStreamingState& state);
 	static void ZeroCLodGroupChunkCounts(ClusterLODGroupChunk& chunk);
	bool ApplyCLodGroupResidency(CLodSharedStreamingState& state, uint32_t groupLocalIndex, bool resident, bool uploadTableImmediately);
	void UploadDirtyCLodGroupChunkTables(const std::vector<std::shared_ptr<CLodSharedStreamingState>>& touchedSharedStates);
	void RebuildCLodSharedStreamingRangeIndex();
	std::shared_ptr<CLodSharedStreamingState> FindCLodSharedStreamingStateByGlobalGroup(uint32_t groupGlobalIndex, uint32_t& outGroupLocalIndex);

	ViewManager* m_pViewManager;

	// Page pool for CLod streaming
	std::unique_ptr<PagePool> m_clodPagePool;

	size_t PackGroupPayloadForPagePool(
		const CLodDiskStreamingResult& result,
		const Mesh* mesh,
		std::vector<std::byte>& outBlob,
		ClusterLODGroupChunk& inOutChunk);
};