#pragma once

#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ShaderBuffers.h"
#include "Mesh/Mesh.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
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

	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	~MeshManager();
	void AddMesh(std::shared_ptr<Mesh>& mesh, bool useMeshletReorderedVertices);
	void AddMeshInstance(MeshInstance* mesh, bool useMeshletReorderedVertices);
	void RemoveMesh(Mesh* mesh);
	void RemoveMeshInstance(MeshInstance* mesh);

	bool SetCLodGroupResidencyForInstance(uint32_t meshInstanceIndex, uint32_t groupGlobalIndex, bool resident);
	uint32_t SetCLodGroupResidencyForGlobal(uint32_t groupGlobalIndex, bool resident);
	void GetCLodActiveUniqueAssetGroupRanges(std::vector<CLodActiveGroupRange>& outRanges, uint32_t& outMaxGroupIndex) const;
	void GetCLodCoarsestUniqueAssetGroupRanges(std::vector<CLodActiveGroupRange>& outRanges) const;
	void GetCLodUniqueAssetParentMap(std::vector<int32_t>& outParentGroupByGlobal, uint32_t& outMaxGroupIndex) const;
	void ProcessCLodDiskStreamingIO(uint32_t maxCompletedRequests = 64u);

	void UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data);
	void UpdatePerMeshInstanceBuffer(std::unique_ptr<BufferView>& view, PerMeshInstanceCB& data);
	void SetViewManager(ViewManager* viewManager) { m_pViewManager = viewManager; }
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
	std::shared_ptr<DynamicBuffer> m_clodCompressedPositions;
	std::shared_ptr<DynamicBuffer> m_clodCompressedNormals;
	std::shared_ptr<DynamicBuffer> m_clodCompressedMeshletVertexIndices;
	//std::shared_ptr<DynamicBuffer> m_meshletBoundsBuffer;
	//std::shared_ptr<DynamicBuffer> m_meshletBitfieldBuffer;
	//std::shared_ptr<DynamicBuffer> m_clusterToVisibleClusterTableIndexBuffer; // Used by visibility buffer, for drawcall indexing

	// Base meshes
	std::shared_ptr<DynamicBuffer> m_perMeshBuffers;

	// mesh instances
	std::shared_ptr<DynamicBuffer> m_perMeshInstanceBuffers;

	std::shared_ptr<DynamicBuffer> m_perMeshInstanceClodOffsets;
	std::shared_ptr<DynamicBuffer> m_perMeshInstanceClodGroupChunks;
	std::shared_ptr<DynamicBuffer> m_clusterLODGroups;
	std::shared_ptr<DynamicBuffer> m_clusterLODChildren;

	//std::shared_ptr<DynamicBuffer> m_clusterLODMeshlets;
	std::shared_ptr<DynamicBuffer> m_clusterLODMeshletBounds;
	std::shared_ptr<DynamicBuffer> m_clusterLODNodes;
	uint64_t m_activeMeshletCount = 0;

	struct CLodInstanceStreamingState {
		MeshInstance* instance = nullptr;
		uint32_t meshInstanceIndex = 0;
		uint32_t groupsBase = 0;
		uint32_t groupCount = 0;
		BufferView* groupChunksView = nullptr;
		std::vector<ClusterLODGroupChunk> baselineGroupChunks;
		std::vector<uint8_t> groupResidentFlags;
	};

	std::unordered_map<uint32_t, CLodInstanceStreamingState> m_clodStreamingStatesByInstanceIndex;
	std::unordered_map<const MeshInstance*, uint32_t> m_clodStreamingInstanceLookup;

	struct CLodDiskStreamingRequest {
		uint32_t groupGlobalIndex = 0;
		ClusterLODCacheSource cacheSource{};
		uint32_t groupLocalIndex = 0;
	};

	struct CLodDiskStreamingResult {
		uint32_t groupGlobalIndex = 0;
		uint32_t groupLocalIndex = 0;
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

	std::thread m_clodDiskStreamingThread;
	std::mutex m_clodDiskStreamingMutex;
	std::condition_variable m_clodDiskStreamingCv;
	bool m_clodDiskStreamingStop = false;
	std::deque<CLodDiskStreamingRequest> m_clodDiskStreamingRequests;
	std::deque<CLodDiskStreamingResult> m_clodDiskStreamingResults;
	std::unordered_set<uint32_t> m_clodDiskStreamingQueuedGroups;

	void CLodDiskStreamingWorkerMain();
	bool QueueCLodDiskStreamingRequest(uint32_t groupGlobalIndex, CLodInstanceStreamingState& state, uint32_t groupLocalIndex);
	void ApplyCompletedCLodDiskStreamingResult(CLodDiskStreamingResult& result);
	void UploadCLodGroupChunkTable(const CLodInstanceStreamingState& state);
 	bool IsCLodGroupResident(const CLodInstanceStreamingState& state, uint32_t groupLocalIndex) const;
	bool IsAnyCLodInstanceResidentForGlobalGroup(uint32_t groupGlobalIndex) const;
	void DeallocateCLodGroupChunkViews(Mesh& mesh, uint32_t groupLocalIndex);
 	static void ZeroCLodGroupChunkCounts(ClusterLODGroupChunk& chunk);

	bool ApplyCLodGroupResidency(CLodInstanceStreamingState& state, uint32_t groupLocalIndex, bool resident);

	ViewManager* m_pViewManager;
};