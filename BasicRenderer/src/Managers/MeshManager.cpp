#include "Managers/MeshManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Mesh/Mesh.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Buffers/BufferView.h"
#include "Mesh/MeshInstance.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/Buffers/PagePool.h"
#include "Managers/ViewManager.h"
#include "Import/CLodCache.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <cassert>
#include "../../generated/BuiltinResources.h"
#include "Render/MemoryIntrospectionAPI.h"

MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_preSkinningVertices = DynamicBuffer::CreateShared(1, 4, "preSkinnedVertices", true);
	m_postSkinningVertices = DynamicBuffer::CreateShared(1, 4, "PostSkinnedvertices", true, true);
	m_meshletOffsets = DynamicBuffer::CreateShared(sizeof(meshopt_Meshlet), 1, "meshletOffsets");
	m_meshletVertexIndices = DynamicBuffer::CreateShared(sizeof(unsigned int), 1, "meshletVertexIndices");
	m_meshletTriangles = DynamicBuffer::CreateShared(1, 4, "meshletTriangles", true);

	m_perMeshBuffers = DynamicBuffer::CreateShared(sizeof(PerMeshCB), 1, "PerMeshBuffers");
	m_perMeshInstanceBuffers = DynamicBuffer::CreateShared(sizeof(PerMeshCB), 1, "perMeshInstanceBuffers");

	// Cluster LOD data
	m_perMeshInstanceClodOffsets = DynamicBuffer::CreateShared(sizeof(MeshInstanceClodOffsets), 10000, "perMeshInstanceClodOffsets");
	m_clodSharedGroupChunks = DynamicBuffer::CreateShared(sizeof(ClusterLODGroupChunk), 10000, "clodSharedGroupChunks");
	m_clodMeshMetadata = DynamicBuffer::CreateShared(sizeof(CLodMeshMetadata), 10000, "clodMeshMetadata");
	m_clusterLODGroups = DynamicBuffer::CreateShared(sizeof(ClusterLODGroup), 10000, "clusterLODGroups");
	m_clusterLODSegments = DynamicBuffer::CreateShared(sizeof(ClusterLODGroupSegment), 10000, "clusterLODSegments");
	m_clusterLODMeshletBounds = DynamicBuffer::CreateShared(sizeof(BoundingSphere), 10000, "clusterLODMeshletBounds", false, true);
	m_clusterLODNodes = DynamicBuffer::CreateShared(sizeof(ClusterLODNode), 10000, "clusterLODNodes");
	m_clodGroupPageMap = DynamicBuffer::CreateShared(sizeof(GroupPageMapEntry), 10000, "clodGroupPageMap");

	m_postSkinningVertices->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_meshletOffsets->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_meshletVertexIndices->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_meshletTriangles->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_clusterLODMeshletBounds->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_clodSharedGroupChunks->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_clodGroupPageMap->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);

	// Tag resources for memory statistics
	rg::memory::SetResourceUsageHint(*m_preSkinningVertices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_postSkinningVertices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletOffsets, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletVertexIndices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletTriangles, "Mesh Data");

	rg::memory::SetResourceUsageHint(*m_perMeshBuffers, "PerMesh, PerMeshInstance, PerObject");
	rg::memory::SetResourceUsageHint(*m_perMeshInstanceBuffers, "PerMesh, PerMeshInstance, PerObject");


	m_resources[Builtin::PreSkinningVertices] = m_preSkinningVertices;
	m_resources[Builtin::PostSkinningVertices] = m_postSkinningVertices;
	m_resources[Builtin::PerMeshBuffer] = m_perMeshBuffers;
	m_resources[Builtin::PerMeshInstanceBuffer] = m_perMeshInstanceBuffers;
	m_resources[Builtin::MeshResources::MeshletOffsets] = m_meshletOffsets;
	m_resources[Builtin::MeshResources::MeshletVertexIndices] = m_meshletVertexIndices;
	m_resources[Builtin::MeshResources::MeshletTriangles] = m_meshletTriangles;

	m_resources[Builtin::CLod::Offsets] = m_perMeshInstanceClodOffsets;
	m_resources[Builtin::CLod::GroupChunks] = m_clodSharedGroupChunks;
	m_resources[Builtin::CLod::MeshMetadata] = m_clodMeshMetadata;
	m_resources[Builtin::CLod::Groups] = m_clusterLODGroups;
	m_resources[Builtin::CLod::Segments] = m_clusterLODSegments;
	m_resources[Builtin::CLod::MeshletBounds] = m_clusterLODMeshletBounds;
	m_resources[Builtin::CLod::Nodes] = m_clusterLODNodes;
	m_resources[Builtin::CLod::GroupPageMap] = m_clodGroupPageMap;

	// Page pool
	{
		PagePool::Config ppConfig;
		ppConfig.pageSize     = 256 * 1024;         // 256 KB
		ppConfig.slabSize     = 256 * 1024 * 1024;  // 256 MB
		ppConfig.numStreamingSlabs = 16;
		ppConfig.debugName    = "CLodPagePool";
		m_clodPagePool = std::make_unique<PagePool>(ppConfig);
	}
	m_resources[Builtin::CLod::PageTable] = m_clodPagePool->GetPageTableBuffer();
	// Slab buffers are registered dynamically as they're allocated.
	// The PagePoolSlabBase descriptor is resolved per-pass from the first slab.

}

MeshManager::~MeshManager() {
}

void MeshManager::InvalidateCLodDiskStreamingPipeline() {
	// Bump generation so in-flight IO tasks produce stale results that will be rejected.
	m_clodDiskStreamingGeneration.fetch_add(1, std::memory_order_release);

	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		m_clodDiskStreamingRequests.clear();
		m_clodDiskStreamingQueuedGroups.clear();
	}
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingResultsMutex);
		m_clodDiskStreamingResults.clear();
		m_clodDiskStreamingCompletions.clear();
	}
}

void MeshManager::DispatchCLodDiskStreamingBatch() {
	// Drain up to kMaxIoBatchSize highest-priority requests from the pending queue.
	std::vector<CLodDiskStreamingRequest> batch;
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		if (m_clodDiskStreamingRequests.empty()) {
			return;
		}

		// Sort so highest-priority requests are at the back.
		std::sort(m_clodDiskStreamingRequests.begin(), m_clodDiskStreamingRequests.end(),
			[](const CLodDiskStreamingRequest& a, const CLodDiskStreamingRequest& b) {
				return a.priority < b.priority;
			});

		const uint32_t toDrain = std::min<uint32_t>(kMaxIoBatchSize, static_cast<uint32_t>(m_clodDiskStreamingRequests.size()));
		// Take from the back (highest priority).
		batch.reserve(toDrain);
		for (uint32_t i = 0; i < toDrain; ++i) {
			batch.push_back(std::move(m_clodDiskStreamingRequests[m_clodDiskStreamingRequests.size() - 1 - i]));
		}
		m_clodDiskStreamingRequests.resize(m_clodDiskStreamingRequests.size() - toDrain);
	}

	// Dispatch each request as a fire-and-forget IO task on the dedicated IO
	// thread pool. Each task captures its request by move, performs the disk
	// read, and pushes the result directly into the shared results vector.
	auto& scheduler = TaskSchedulerManager::GetInstance();
	for (auto& request : batch) {
		scheduler.QueueIoTask("CLodDiskStreaming",
			[this, request = std::move(request)]() mutable {
			CLodDiskStreamingResult result{};
			result.groupGlobalIndex = request.groupGlobalIndex;
			result.generation = request.generation;

			struct TLContainerState {
				std::wstring containerFileName;
				std::string sourceIdentifier;
				std::ifstream file;
				uint32_t groupCount = 0;
				bool valid = false;
			};
			thread_local TLContainerState tls;

			if (!tls.valid
				|| tls.containerFileName != request.cacheSource.containerFileName
				|| tls.sourceIdentifier != request.cacheSource.sourceIdentifier) {
				tls.file.close();
				tls.valid = false;
				tls.containerFileName = request.cacheSource.containerFileName;
				tls.sourceIdentifier = request.cacheSource.sourceIdentifier;
				tls.groupCount = 0;
				if (CLodCache::OpenContainerFile(request.cacheSource, tls.file, tls.groupCount)) {
					tls.valid = true;
				}
			}

			if (!tls.valid || request.groupLocalIndex >= tls.groupCount) {
				result.success = false;
				std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
				m_clodDiskStreamingResults.push_back(std::move(result));
				return;
			}

			const uint64_t directoryEntryOffset =
				static_cast<uint64_t>(sizeof(uint32_t) * 4)
				+ static_cast<uint64_t>(request.groupLocalIndex) * static_cast<uint64_t>(sizeof(ClusterLODGroupDiskLocator));

			tls.file.seekg(static_cast<std::streamoff>(directoryEntryOffset), std::ios::beg);
			if (!tls.file.good()) {
				result.success = false;
				tls.valid = false;
				std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
				m_clodDiskStreamingResults.push_back(std::move(result));
				return;
			}

			ClusterLODGroupDiskLocator locator{};
			tls.file.read(reinterpret_cast<char*>(&locator), sizeof(locator));
			if (!tls.file.good()) {
				result.success = false;
				tls.valid = false;
				std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
				m_clodDiskStreamingResults.push_back(std::move(result));
				return;
			}

			CLodCache::LoadedGroupPayload payload{};
			if (CLodCache::LoadGroupPayloadSelective(tls.file, locator, request.segmentNeedsFetch, payload)) {
				result.groupChunkMetadata = payload.groupChunkMetadata;
				result.pageBlobs = std::move(payload.pageBlobs);
				result.preAllocatedPages = std::move(request.preAllocatedPages);
				result.success = true;
			}
			else {
				result.success = false;
				tls.file.clear();
			}

			std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
			m_clodDiskStreamingResults.push_back(std::move(result));
		});
	}
}

void MeshManager::AddMesh(std::shared_ptr<Mesh>& mesh, bool useMeshletReorderedVertices) {
	mesh->SetCurrentMeshManager(this);
	if (mesh->GetPreSkinningVertexBufferView() != nullptr) {
		m_preSkinningVertices->Deallocate(mesh->GetPreSkinningVertexBufferView());
		mesh->SetPreSkinningVertexBufferView(nullptr);
	}
	if (mesh->GetPostSkinningVertexBufferView() != nullptr) {
		m_postSkinningVertices->Deallocate(mesh->GetPostSkinningVertexBufferView());
		mesh->SetPostSkinningVertexBufferView(nullptr);
	}
	for (const auto& [_, chunkView] : mesh->GetCLodPreSkinningVertexChunkViews()) {
		if (chunkView) {
			m_preSkinningVertices->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodPostSkinningVertexChunkViews()) {
		if (chunkView) {
			m_postSkinningVertices->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodMeshletVertexChunkViews()) {
		if (chunkView) {
			m_meshletVertexIndices->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodMeshletChunkViews()) {
		if (chunkView) {
			m_meshletOffsets->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodMeshletTriangleChunkViews()) {
		if (chunkView) {
			m_meshletTriangles->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodMeshletBoundsChunkViews()) {
		if (chunkView) {
			m_clusterLODMeshletBounds->Deallocate(chunkView.get());
		}
	}
	mesh->SetCLodGroupChunkViews({}, {}, {}, {}, {}, {}, {}, {}, {});

	const auto& groupDiskLocators = mesh->GetCLodGroupDiskLocators();

	std::unique_ptr<BufferView> postSkinningView = nullptr;
	std::unique_ptr<BufferView> preSkinningView = nullptr;
	size_t vertexByteSize = mesh->GetPerMeshCBData().vertexByteSize;
	std::vector<std::unique_ptr<BufferView>> clodPreSkinningChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodPostSkinningChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodMeshletVertexChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodCompressedPositionChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodCompressedNormalChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodCompressedMeshletVertexChunkViews;
 	std::vector<std::unique_ptr<BufferView>> clodMeshletChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodMeshletTriangleChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodMeshletBoundsChunkViews;

	const bool hasDiskBackedGroupChunks = !groupDiskLocators.empty() && (groupDiskLocators.size() == mesh->GetCLodGroupCount());
	const bool preferDiskBackedStreaming = hasDiskBackedGroupChunks && mesh->HasCLodDiskStreamingSource();
	if (mesh->GetPerMeshCBData().vertexFlags & VertexFlags::VERTEX_SKINNED) {
		unsigned int skinningVertexByteSize = mesh->GetSkinningVertexSize();
		//preSkinningView = m_preSkinningVertices->AddData(skinningVertices.data(), numVertices * skinningVertexByteSize, skinningVertexByteSize);
	}
	else {
		//postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexByteSize, vertexByteSize);
		//meshletBoundsView = m_meshletBoundsBuffer->AddData(mesh->GetMeshletBounds().data(), mesh->GetMeshletCount() * sizeof(BoundingSphere), sizeof(BoundingSphere));
	}

	if (!preferDiskBackedStreaming) {
		throw std::runtime_error("CLod disk streaming metadata is required; non-disk CLOD upload path has been removed.");
	}

	// Per mesh buffer
	auto perMeshBufferView = m_perMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), sizeof(PerMeshCB));
	mesh->SetPerMeshBufferView(std::move(perMeshBufferView));

	// Vertex data
	if (preSkinningView) {
		mesh->SetPreSkinningVertexBufferView(std::move(preSkinningView));
	}
	if (postSkinningView) {
		mesh->SetPostSkinningVertexBufferView(std::move(postSkinningView));
	}
	mesh->SetCLodGroupChunkViews(
		std::move(clodPreSkinningChunkViews),
		std::move(clodPostSkinningChunkViews),
		std::move(clodMeshletVertexChunkViews),
		std::move(clodCompressedPositionChunkViews),
		std::move(clodCompressedNormalChunkViews),
		std::move(clodCompressedMeshletVertexChunkViews),
		std::move(clodMeshletChunkViews),
		std::move(clodMeshletTriangleChunkViews),
		std::move(clodMeshletBoundsChunkViews));
	//mesh->SetMeshletBoundsBufferView(std::move(meshletBoundsView));

	// Cluster LOD hierarchy data is shared per mesh; per-instance state stores only indirection/instance IDs.
	auto clusterLODGroupsView = m_clusterLODGroups->AddData(mesh->GetCLodGroups().data(), mesh->GetCLodGroups().size() * sizeof(ClusterLODGroup), sizeof(ClusterLODGroup));
	auto clusterLODSegmentsView = m_clusterLODSegments->AddData(mesh->GetCLodSegments().data(), mesh->GetCLodSegments().size() * sizeof(ClusterLODGroupSegment), sizeof(ClusterLODGroupSegment));
	
	auto clusterLODNodesView = m_clusterLODNodes->AddData(mesh->GetCLodNodes().data(), mesh->GetCLodNodes().size() * sizeof(ClusterLODNode), sizeof(ClusterLODNode));

	// Create shared streaming state (once per mesh, before hierarchy CPU data is released)
	{
		const uint32_t groupsBase = static_cast<uint32_t>(clusterLODGroupsView->GetOffset() / sizeof(ClusterLODGroup));
		const auto& groupChunkHints = mesh->GetCLodGroupChunkHints();
		std::vector<ClusterLODGroupChunk> baselineGroupChunks(groupChunkHints.size());
		std::vector<ClusterLODGroupChunk> materializedGroupChunks(groupChunkHints.size());
		std::vector<uint8_t> groupResidentFlags(groupChunkHints.size(), 1u);

		for (size_t groupIndex = 0; groupIndex < groupChunkHints.size(); ++groupIndex)
		{
			ClusterLODGroupChunk chunk{};
			const auto& hint = groupChunkHints[groupIndex];
			chunk.groupVertexCount = hint.groupVertexCount;
			chunk.meshletCount = hint.meshletCount;
			chunk.meshletTrianglesByteCount = hint.meshletTrianglesByteCount;

			// Fresh chunks start non-resident, so expose zero counts to the GPU.
			bool hasRuntimeChunkData = (chunk.meshletCount == 0u);
			baselineGroupChunks[groupIndex] = chunk;

			if (!hasRuntimeChunkData) {
				chunk.groupVertexCount = 0;
				chunk.meshletCount = 0;
				chunk.meshletTrianglesByteCount = 0;
				groupResidentFlags[groupIndex] = 0u;
			}

			materializedGroupChunks[groupIndex] = chunk;
		}

		std::unique_ptr<BufferView> sharedGroupChunksView = nullptr;
		if (!materializedGroupChunks.empty())
		{
			sharedGroupChunksView = m_clodSharedGroupChunks->AddData(
				materializedGroupChunks.data(),
				materializedGroupChunks.size() * sizeof(ClusterLODGroupChunk),
				sizeof(ClusterLODGroupChunk));
		}

		auto sharedState = std::make_shared<CLodSharedStreamingState>();
		sharedState->mesh = mesh.get();

		// Move hierarchy data into the shared state before the mesh releases its CPU copies.
		sharedState->groups = mesh->GetCLodGroups();
		sharedState->segments = mesh->GetCLodSegments();

		// Cache parent-child mapping and error values for streaming snapshots.
		{
			const auto& summary = mesh->GetCLodRuntimeSummary();
			sharedState->parentGroupByLocal = summary.parentGroupByLocal;
			sharedState->groupErrorByLocal = summary.groupErrorByLocal;
		}

		// Compute total page-map entries needed for all groups in this mesh.
		uint32_t totalPageMapEntries = 0;
		for (const auto& grp : sharedState->groups) {
			totalPageMapEntries += grp.pageCount;
		}

		// Allocate a contiguous range in the GroupPageMap buffer.
		std::unique_ptr<BufferView> pageMapView = nullptr;
		uint32_t pageMapGlobalBase = 0;
		if (totalPageMapEntries > 0) {
			std::vector<GroupPageMapEntry> initialPageMapEntries(totalPageMapEntries); // zero-init
			pageMapView = m_clodGroupPageMap->AddData(
				initialPageMapEntries.data(),
				totalPageMapEntries * sizeof(GroupPageMapEntry),
				sizeof(GroupPageMapEntry));
			if (pageMapView) {
				pageMapGlobalBase = static_cast<uint32_t>(pageMapView->GetOffset() / sizeof(GroupPageMapEntry));
			}
		}

		CLodMeshMetadata clodMeshMetadata{};
		clodMeshMetadata.groupsBase = groupsBase;
		clodMeshMetadata.segmentsBase = static_cast<uint32_t>(clusterLODSegmentsView->GetOffset() / sizeof(ClusterLODGroupSegment));
		clodMeshMetadata.lodNodesBase = static_cast<uint32_t>(clusterLODNodesView->GetOffset() / sizeof(ClusterLODNode));
		clodMeshMetadata.rootNode = mesh->GetCLodRootNodeIndex();
		clodMeshMetadata.groupChunkTableBase = (sharedGroupChunksView != nullptr)
			? static_cast<uint32_t>(sharedGroupChunksView->GetOffset() / sizeof(ClusterLODGroupChunk))
			: 0u;
		clodMeshMetadata.groupChunkTableCount = static_cast<uint32_t>(materializedGroupChunks.size());
		clodMeshMetadata.pageMapBase = pageMapGlobalBase;
		sharedState->ownedMeshMetadataView = m_clodMeshMetadata->AddData(&clodMeshMetadata, sizeof(CLodMeshMetadata), sizeof(CLodMeshMetadata));
		if (sharedState->ownedMeshMetadataView != nullptr) {
			sharedState->clodMeshMetadataIndex = static_cast<uint32_t>(sharedState->ownedMeshMetadataView->GetOffset() / sizeof(CLodMeshMetadata));
		}

		sharedState->groupsBase = groupsBase;
		sharedState->groupCount = static_cast<uint32_t>(materializedGroupChunks.size());
		sharedState->ownedGroupChunksView = std::move(sharedGroupChunksView);
		sharedState->groupChunksView = sharedState->ownedGroupChunksView.get();
		sharedState->baselineGroupChunks = std::move(baselineGroupChunks);
		sharedState->groupResidentFlags = std::move(groupResidentFlags);
		sharedState->ownedPageMapView = std::move(pageMapView);
		sharedState->pageMapGlobalBase = pageMapGlobalBase;
		sharedState->totalPageMapEntries = totalPageMapEntries;
		sharedState->pageMapEntriesCPU.resize(totalPageMapEntries);
		sharedState->residentGroupAllocations.resize(sharedState->groupCount);

		m_clodSharedStreamingStateByMesh[mesh.get()] = sharedState;
		m_clodSharedStreamingRangesDirty = true;
		m_clodStreamingStructureDirty = true;

	}

	mesh->SetCLodBufferViews(
		std::move(clusterLODGroupsView), 
		std::move(clusterLODSegmentsView), 
		std::move(clusterLODNodesView));
	mesh->ReleaseCLodChunkUploadData();
	mesh->ReleaseCLodHierarchyCpuData();
	mesh->ReleaseCLodGroupChunkMetadataCpuData();

}

void MeshManager::RemoveMesh(Mesh* mesh) {

	// Things to remove:
	// - Pre-skinning vertices, if any
	// - Post-skinning vertices
	// - Meshlet bounds

	auto preSkinningView = mesh->GetPreSkinningVertexBufferView();
	if (preSkinningView != nullptr) {
		m_preSkinningVertices->Deallocate(preSkinningView);
	}
	auto postSkinningView = mesh->GetPostSkinningVertexBufferView();
	if (postSkinningView != nullptr) {
		m_postSkinningVertices->Deallocate(postSkinningView);
	}
	for (const auto& [_, chunkView] : mesh->GetCLodPreSkinningVertexChunkViews()) {
		if (chunkView) {
			m_preSkinningVertices->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodPostSkinningVertexChunkViews()) {
		if (chunkView) {
			m_postSkinningVertices->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodMeshletVertexChunkViews()) {
		if (chunkView) {
			m_meshletVertexIndices->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodMeshletChunkViews()) {
		if (chunkView) {
			m_meshletOffsets->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodMeshletTriangleChunkViews()) {
		if (chunkView) {
			m_meshletTriangles->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodMeshletBoundsChunkViews()) {
		if (chunkView) {
			m_clusterLODMeshletBounds->Deallocate(chunkView.get());
		}
	}
	// Deallocate the per mesh buffer view
	auto& perMeshBufferView = mesh->GetPerMeshBufferView();
	if (perMeshBufferView != nullptr) {
		m_perMeshBuffers->Deallocate(perMeshBufferView.get());
	}

	mesh->SetPerMeshBufferView(nullptr);
	mesh->SetCurrentMeshManager(nullptr);
}

void MeshManager::AddMeshInstance(MeshInstance* mesh, bool useMeshletReorderedVertices) {
	mesh->SetCurrentMeshManager(this);
	(void)useMeshletReorderedVertices;

	if (mesh->HasSkin()) { // Skinned meshes need unique post-skinning vertex buffers
		// TODO: CLod skinning
	}
	else { // Non-skinned meshes can share post-skinning vertex buffers
		auto perMeshInstanceBufferView = m_perMeshInstanceBuffers->AddData(&mesh->GetPerMeshInstanceBufferData(), sizeof(PerMeshInstanceCB), sizeof(PerMeshInstanceCB));
		mesh->SetBufferViewUsingBaseMesh(std::move(perMeshInstanceBufferView));
	}

	uint32_t bitsToAllocate = mesh->GetMesh()->GetCLodMeshletCount();
	m_activeMeshletCount += bitsToAllocate;

	uint32_t perMeshIndex = static_cast<uint32_t>(
		mesh->GetMesh()->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
	mesh->SetPerMeshBufferIndex(perMeshIndex);

	auto meshPtr = mesh->GetMesh().get();
	auto sharedStateIt = m_clodSharedStreamingStateByMesh.find(meshPtr);
	std::shared_ptr<CLodSharedStreamingState> sharedState;
	if (sharedStateIt != m_clodSharedStreamingStateByMesh.end()) {
		sharedState = sharedStateIt->second;
	}

	if (sharedState) {
		sharedState->activeInstanceCount++;
	}

	MeshInstanceClodOffsets clodOffsets = {};
	clodOffsets.clodMeshMetadataIndex = (sharedState != nullptr) ? sharedState->clodMeshMetadataIndex : 0u;
	//clodOffsets.rootGroup = mesh->GetMesh()->GetCLodRootGroup();
	auto clodOffsetsView = m_perMeshInstanceClodOffsets->AddData(&clodOffsets, sizeof(MeshInstanceClodOffsets), sizeof(MeshInstanceClodOffsets)); // Indexable by mesh instance

	mesh->SetCLodBufferViews(std::move(clodOffsetsView));

	if (sharedState != nullptr && sharedState->groupCount > 0u) {
		CLodStreamingInstanceState state{};
		state.instance = mesh;
		state.meshInstanceIndex = static_cast<uint32_t>(mesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
		state.groupsBase = sharedState->groupsBase;
		state.groupCount = sharedState->groupCount;
		state.sharedMeshState = sharedState;

		m_clodStreamingStateByInstanceIndex[state.meshInstanceIndex] = std::move(state);
		m_clodStreamingInstanceIndexByPtr[mesh] = static_cast<uint32_t>(mesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
	}
}

void MeshManager::RemoveMeshInstance(MeshInstance* mesh) {

	// Things to remove:
	// - Post-skinning vertices
	// - Per-mesh instance buffer
	// - Meshlet bounds

	auto perMeshInstanceBufferView = mesh->GetPerMeshInstanceBufferView();
	if (perMeshInstanceBufferView != nullptr) {
		m_perMeshInstanceBuffers->Deallocate(perMeshInstanceBufferView);
	}
	mesh->SetBufferViews(nullptr);
	m_activeMeshletCount -= mesh->GetMesh()->GetCLodMeshletCount();

	auto clodBuffersView = mesh->GetCLodOffsetsView();
	if (clodBuffersView != nullptr) {
		m_perMeshInstanceClodOffsets->Deallocate(clodBuffersView);
	}
	mesh->SetCLodBufferViews(nullptr);

	auto itLookup = m_clodStreamingInstanceIndexByPtr.find(mesh);
	if (itLookup != m_clodStreamingInstanceIndexByPtr.end()) {
		auto itState = m_clodStreamingStateByInstanceIndex.find(itLookup->second);
		if (itState != m_clodStreamingStateByInstanceIndex.end()) {
			auto sharedMeshState = itState->second.sharedMeshState;
			if (sharedMeshState != nullptr && sharedMeshState->activeInstanceCount > 0u) {
				sharedMeshState->activeInstanceCount--;
				if (sharedMeshState->activeInstanceCount == 0u) {
					ReleaseAllCLodGroupChunkAllocations(*sharedMeshState);
					if (sharedMeshState->ownedMeshMetadataView != nullptr) {
						m_clodMeshMetadata->Deallocate(sharedMeshState->ownedMeshMetadataView.get());
						sharedMeshState->ownedMeshMetadataView = nullptr;
					}
					if (sharedMeshState->ownedGroupChunksView != nullptr) {
						m_clodSharedGroupChunks->Deallocate(sharedMeshState->ownedGroupChunksView.get());
						sharedMeshState->groupChunksView = nullptr;
						sharedMeshState->ownedGroupChunksView = nullptr;
					}
					m_clodSharedStreamingStateByMesh.erase(mesh->GetMesh().get());
					m_clodSharedStreamingRangesDirty = true;
					m_clodStreamingStructureDirty = true;
				}
			}
		}
		m_clodStreamingStateByInstanceIndex.erase(itLookup->second);
		m_clodStreamingInstanceIndexByPtr.erase(itLookup);
	}
}

void MeshManager::ProcessCLodDiskStreamingIO(
	uint32_t maxCompletedRequests) {
	// Dispatch pending IO requests across the task scheduler's IO workers.
	DispatchCLodDiskStreamingBatch();

	// Drain completed results into a local vector under the results lock.
	std::vector<CLodDiskStreamingResult> localResults;
	{
		std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
		const uint32_t toDrain = std::min<uint32_t>(maxCompletedRequests,
			static_cast<uint32_t>(m_clodDiskStreamingResults.size()));
		if (toDrain > 0u) {
			localResults.reserve(toDrain);
			for (uint32_t i = 0; i < toDrain; ++i) {
				localResults.push_back(std::move(m_clodDiskStreamingResults[i]));
			}
			m_clodDiskStreamingResults.erase(m_clodDiskStreamingResults.begin(),
				m_clodDiskStreamingResults.begin() + toDrain);
		}
	}

	// Apply each result under the residency lock.
	if (!localResults.empty()) {
		const uint64_t currentGeneration = m_clodDiskStreamingGeneration.load(std::memory_order_acquire);
		std::vector<CLodDiskStreamingCompletion> newCompletions;
		newCompletions.reserve(localResults.size());
		for (auto& result : localResults) {
			// Reject stale results from a previous generation (pre-rebuild IO).
			if (result.generation != currentGeneration) {
				spdlog::info("CLod streaming: rejecting stale IO result for group {} (gen {} vs current {})",
					result.groupGlobalIndex, result.generation, currentGeneration);
				newCompletions.push_back({ result.groupGlobalIndex, false });
				continue;
			}
			DiskStreamingApplyResult applyResult;
			{
				std::lock_guard<std::mutex> residencyLock(m_clodResidencyMutex);
				applyResult = ApplyCompletedCLodDiskStreamingResult(result, result.preAllocatedPages);
			}
			newCompletions.push_back({ result.groupGlobalIndex,
				applyResult == DiskStreamingApplyResult::Applied });
		}

		// Now that all results are applied, remove them from the dedup set.
		{
			std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
			for (auto& result : localResults) {
				m_clodDiskStreamingQueuedGroups.erase(result.groupGlobalIndex);
			}
		}

		{
			std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
			for (auto& c : newCompletions) {
				m_clodDiskStreamingCompletions.push_back(std::move(c));
			}
		}
	}
}



void MeshManager::RebuildCLodSharedStreamingRangeIndex() {
	if (!m_clodSharedStreamingRangesDirty) {
		return;
	}

	m_clodSharedStreamingRanges.clear();
	m_clodSharedStreamingRanges.reserve(m_clodSharedStreamingStateByMesh.size());

	for (const auto& [_, sharedState] : m_clodSharedStreamingStateByMesh) {
		if (sharedState == nullptr || sharedState->groupCount == 0u) {
			continue;
		}

		CLodSharedStreamingRange range{};
		range.begin = sharedState->groupsBase;
		range.end = sharedState->groupsBase + sharedState->groupCount;
		range.state = sharedState;
		m_clodSharedStreamingRanges.push_back(std::move(range));
	}

	std::sort(m_clodSharedStreamingRanges.begin(), m_clodSharedStreamingRanges.end(), [](const CLodSharedStreamingRange& a, const CLodSharedStreamingRange& b) {
		return a.begin < b.begin;
	});

	m_clodSharedStreamingRangesDirty = false;
}

std::shared_ptr<MeshManager::CLodSharedStreamingState> MeshManager::FindCLodSharedStreamingStateByGlobalGroup(uint32_t groupGlobalIndex, uint32_t& outGroupLocalIndex) {
	outGroupLocalIndex = 0u;
	RebuildCLodSharedStreamingRangeIndex();

	if (m_clodSharedStreamingRanges.empty()) {
		return nullptr;
	}

	auto it = std::upper_bound(
		m_clodSharedStreamingRanges.begin(),
		m_clodSharedStreamingRanges.end(),
		groupGlobalIndex,
		[](uint32_t value, const CLodSharedStreamingRange& range) {
			return value < range.begin;
		});

	if (it == m_clodSharedStreamingRanges.begin()) {
		return nullptr;
	}

	--it;
	if (groupGlobalIndex < it->begin || groupGlobalIndex >= it->end || it->state == nullptr) {
		return nullptr;
	}

	outGroupLocalIndex = groupGlobalIndex - it->begin;
	return it->state;
}

bool MeshManager::QueueCLodDiskStreamingRequest(uint32_t groupGlobalIndex, CLodSharedStreamingState& state, uint32_t groupLocalIndex, bool& outQueued, const std::vector<bool>& segmentNeedsFetch, const std::vector<uint32_t>& preAllocatedPages, uint32_t priority) {
	outQueued = false;
	if (state.mesh == nullptr) {
		return false;
	}

	auto* mesh = state.mesh;
	if (groupLocalIndex >= mesh->GetCLodGroupCount()) {
		return false;
	}
	if (groupLocalIndex >= state.residentGroupAllocations.size()) {
		return false;
	}

	const auto& residentAllocations = state.residentGroupAllocations[groupLocalIndex];
	const auto& groupChunkHints = mesh->GetCLodGroupChunkHints();
	if (groupLocalIndex >= groupChunkHints.size()) {
		return false;
	}
	const auto& sourceChunk = groupChunkHints[groupLocalIndex];

	// The page-pool path considers a group "ready" when the page allocation is valid
	// (meshlets + bounds are packed into the page-pool blob).
	const bool hasRequiredAllocations = !residentAllocations.pageAllocations.empty()
		|| sourceChunk.meshletCount == 0u;

	if (hasRequiredAllocations) {
		return true;
	}

	if (!mesh->HasCLodDiskStreamingSource()) {
		return false;
	}

	const auto& groupDiskLocators = mesh->GetCLodGroupDiskLocators();
	if (groupLocalIndex >= groupDiskLocators.size()) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		if (!m_clodDiskStreamingQueuedGroups.insert(groupGlobalIndex).second) {
			outQueued = true;
			return false;
		}

		CLodDiskStreamingRequest request{};
		request.groupGlobalIndex = groupGlobalIndex;
		request.groupLocalIndex = groupLocalIndex;
		request.cacheSource = mesh->GetCLodCacheSource();
		request.segmentNeedsFetch = segmentNeedsFetch;
		request.preAllocatedPages = preAllocatedPages;
		request.generation = m_clodDiskStreamingGeneration.load(std::memory_order_acquire);
		request.priority = priority;
		m_clodDiskStreamingRequests.push_back(std::move(request));
	}

	outQueued = true;
	return false;
}

MeshManager::DiskStreamingApplyResult MeshManager::ApplyCompletedCLodDiskStreamingResult(CLodDiskStreamingResult& result, const std::vector<uint32_t>& preAllocatedPages) {
	if (!result.success) {
		return DiskStreamingApplyResult::FailedPermanent;
	}

	uint32_t localIndex = 0u;
	auto sharedState = FindCLodSharedStreamingStateByGlobalGroup(result.groupGlobalIndex, localIndex);
	if (sharedState == nullptr || sharedState->mesh == nullptr) {
		return DiskStreamingApplyResult::FailedPermanent;
	}

	auto* mesh = sharedState->mesh;
	if (localIndex >= sharedState->baselineGroupChunks.size() ||
		localIndex >= sharedState->residentGroupAllocations.size()) {
		return DiskStreamingApplyResult::FailedPermanent;
	}

	auto& residentAllocations = sharedState->residentGroupAllocations[localIndex];
	residentAllocations.Reset();

	// Start with baseline chunk and apply any disk-delivered metadata overrides.
	ClusterLODGroupChunk chunk = sharedState->baselineGroupChunks[localIndex];
	if (result.groupChunkMetadata.has_value()) {
		chunk = result.groupChunkMetadata.value();
	}

	const auto& meshGroups = sharedState->groups;
	if (localIndex >= meshGroups.size()) {
		return DiskStreamingApplyResult::FailedPermanent;
	}
	const auto& group = meshGroups[localIndex];
	const uint32_t sCount = group.pageCount;

	if (result.pageBlobs.size() != sCount) {
		spdlog::error("CLod streaming: group {} (local {}) expected {} page blobs but got {}",
			result.groupGlobalIndex, localIndex, sCount, result.pageBlobs.size());
		return DiskStreamingApplyResult::FailedPermanent;
	}

	if (preAllocatedPages.size() != sCount) {
		spdlog::error("CLod streaming: group {} pre-allocated {} pages but expected {}",
			result.groupGlobalIndex, preAllocatedPages.size(), sCount);
		return DiskStreamingApplyResult::FailedPermanent;
	}

	// Upload blobs to pre-allocated pages.
	size_t totalBlobBytes = 0;
	std::vector<GroupPageMapEntry> pageMapEntries(sCount);

	for (uint32_t ci = 0; ci < sCount; ++ci) {
		const auto& blob = result.pageBlobs[ci];
		totalBlobBytes += blob.size();

		const uint32_t pageID = preAllocatedPages[ci];
		PagePool::PageAllocation pageAlloc{ pageID, 1 };

		// Only upload if we fetched this segment (blob non-empty).
		// Skipped segments still have valid data on the slab from a
		// previous partial eviction.
		if (!blob.empty()) {
			m_clodPagePool->UploadToPage(pageID, 0, blob.data(), blob.size());
		}

		pageMapEntries[ci].slabDescriptorIndex = m_clodPagePool->GetSlabDescriptorIndex(pageAlloc);
		pageMapEntries[ci].slabByteOffset = static_cast<uint32_t>(m_clodPagePool->PageToSlabByteOffset(pageID));
		residentAllocations.pageAllocations.push_back(pageAlloc);
	}

	// Upload GroupPageMap entries for this group
	if (sharedState->ownedPageMapView && !pageMapEntries.empty()) {
		const uint32_t pageMapOffset = group.pageMapBase;
		for (uint32_t i = 0; i < sCount; ++i) {
			sharedState->pageMapEntriesCPU[pageMapOffset + i] = pageMapEntries[i];
		}
		m_clodGroupPageMap->UpdateView(
			sharedState->ownedPageMapView.get(),
			sharedState->pageMapEntriesCPU.data());
	}

	// Page slab info is now stored per-page in GroupPageMapEntry, not in the group chunk.

	// Commit chunk updates.
	sharedState->baselineGroupChunks[localIndex] = chunk;
	if (localIndex < sharedState->groupResidentFlags.size()) {
		sharedState->groupResidentFlags[localIndex] = 1u;
	}

	// Debug stats
	{
		m_debugResidentAllocations.fetch_add(static_cast<uint32_t>(residentAllocations.pageAllocations.size()), std::memory_order_relaxed);
		m_debugTotalStreamedBytes.fetch_add(totalBlobBytes, std::memory_order_relaxed);
		m_debugResidentGroups.fetch_add(1u, std::memory_order_relaxed);
	}

	UploadCLodGroupChunkTable(*sharedState);
	return DiskStreamingApplyResult::Applied;
}

void MeshManager::DrainCompletedCLodDiskStreamingGroups(std::vector<CLodDiskStreamingCompletion>& outCompletions) {
	std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
	outCompletions = std::move(m_clodDiskStreamingCompletions);
	m_clodDiskStreamingCompletions.clear();
}

bool MeshManager::FreeCLodGroupEviction(uint32_t groupGlobalIndex) {
	uint32_t localIndex = 0u;
	auto sharedState = FindCLodSharedStreamingStateByGlobalGroup(groupGlobalIndex, localIndex);
	if (sharedState == nullptr) {
		return false;
	}

	std::lock_guard<std::mutex> residencyLock(m_clodResidencyMutex);
	return ApplyCLodGroupEviction(*sharedState, localIndex);
}

bool MeshManager::IsCLodGroupDiskIOQueued(uint32_t groupGlobalIndex) const {
	std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
	return m_clodDiskStreamingQueuedGroups.count(groupGlobalIndex) != 0;
}

bool MeshManager::QueueCLodGroupDiskIO(uint32_t groupGlobalIndex, const std::vector<bool>& segmentNeedsFetch, const std::vector<uint32_t>& preAllocatedPages, uint32_t priority) {
	uint32_t localIndex = 0u;
	auto sharedState = FindCLodSharedStreamingStateByGlobalGroup(groupGlobalIndex, localIndex);
	if (sharedState == nullptr || sharedState->mesh == nullptr) {
		return false;
	}

	bool queued = false;
	QueueCLodDiskStreamingRequest(groupGlobalIndex, *sharedState, localIndex, queued, segmentNeedsFetch, preAllocatedPages, priority);
	return queued;
}

MeshManager::CLodGroupStreamingInfo MeshManager::GetCLodGroupStreamingInfo(uint32_t groupGlobalIndex) const {
	CLodGroupStreamingInfo info{};

	uint32_t localIndex = 0u;
	auto sharedState = const_cast<MeshManager*>(this)->FindCLodSharedStreamingStateByGlobalGroup(groupGlobalIndex, localIndex);
	if (sharedState == nullptr || sharedState->mesh == nullptr) {
		return info;
	}

	const auto& hints = sharedState->mesh->GetCLodGroupChunkHints();
	if (localIndex >= hints.size()) {
		return info;
	}

	info.hint = hints[localIndex];
	info.vertexByteSize = sharedState->mesh->GetPerMeshCBData().vertexByteSize;
	info.valid = true;
	return info;
}

void MeshManager::ZeroCLodGroupChunkCounts(ClusterLODGroupChunk& chunk) {
	chunk.groupVertexCount = 0;
	chunk.meshletCount = 0;
	chunk.meshletTrianglesByteCount = 0;
}

bool MeshManager::IsCLodGroupResident(const CLodSharedStreamingState& state, uint32_t groupLocalIndex) const {
	if (groupLocalIndex >= state.groupResidentFlags.size()) {
		return false;
	}
	return state.groupResidentFlags[groupLocalIndex] != 0u;
}

void MeshManager::DeallocateCLodGroupChunkAllocations(CLodSharedStreamingState& state, uint32_t groupLocalIndex) {
	if (groupLocalIndex >= state.residentGroupAllocations.size()) {
		return;
	}

	auto& residentAllocations = state.residentGroupAllocations[groupLocalIndex];

	// Page ownership is managed by CLodStreamingSystem's page LRU.
	// We only clear metadata here, the pages themselves are returned to the
	// LRU by the streaming system when it evicts the group.

	// Clear page-pool fields in the baseline chunk so the shader sees zeros.
	if (groupLocalIndex < state.baselineGroupChunks.size()) {
		auto& chunk = state.baselineGroupChunks[groupLocalIndex];
		ZeroCLodGroupChunkCounts(chunk);
	}

	// Zero out the GroupPageMap entries for this group.
	// Use state.groups (the retained copy) instead of state.mesh->GetCLodGroups()
	// because the mesh releases its CPU hierarchy data after setup.
	if (state.ownedPageMapView && groupLocalIndex < state.groups.size()) {
		const auto& grp = state.groups[groupLocalIndex];
		if (grp.pageCount > 0) {
			for (uint32_t i = 0; i < grp.pageCount; ++i) {
				state.pageMapEntriesCPU[grp.pageMapBase + i] = {};
			}
			m_clodGroupPageMap->UpdateView(
				state.ownedPageMapView.get(),
				state.pageMapEntriesCPU.data());
		}
	}

	residentAllocations.Reset();
}

void MeshManager::ReleaseAllCLodGroupChunkAllocations(CLodSharedStreamingState& state) {
	for (uint32_t groupLocalIndex = 0u; groupLocalIndex < state.groupCount; ++groupLocalIndex) {
		DeallocateCLodGroupChunkAllocations(state, groupLocalIndex);
	}
}

void MeshManager::UploadCLodGroupChunkTable(const CLodSharedStreamingState& state) {
	if (state.groupChunksView == nullptr || state.baselineGroupChunks.empty()) {
		return;
	}

	std::vector<ClusterLODGroupChunk> materializedGroupChunks = state.baselineGroupChunks;
	const size_t groupCount = std::min(materializedGroupChunks.size(), state.groupResidentFlags.size());
	for (size_t i = 0; i < groupCount; ++i) {
		if (state.groupResidentFlags[i] == 0u) {
			ZeroCLodGroupChunkCounts(materializedGroupChunks[i]);
		}
	}

	m_clodSharedGroupChunks->UpdateView(state.groupChunksView, materializedGroupChunks.data());
}

bool MeshManager::ApplyCLodGroupEviction(CLodSharedStreamingState& state, uint32_t groupLocalIndex) {
	if (state.groupChunksView == nullptr || groupLocalIndex >= state.groupCount || groupLocalIndex >= state.baselineGroupChunks.size()) {
		return false;
	}

	if (groupLocalIndex >= state.groupResidentFlags.size()) {
		return false;
	}

	if (!IsCLodGroupResident(state, groupLocalIndex)) {
		return true; // Already non-resident.
	}

	state.groupResidentFlags[groupLocalIndex] = 0u;
	{
		uint32_t prev = m_debugResidentGroups.load(std::memory_order_relaxed);
		if (prev > 0u) m_debugResidentGroups.store(prev - 1u, std::memory_order_relaxed);
	}
	// Subtract allocation stats before deallocation zeroes the views.
	if (groupLocalIndex < state.residentGroupAllocations.size()) {
		auto& allocs = state.residentGroupAllocations[groupLocalIndex];
		const uint32_t ac = static_cast<uint32_t>(allocs.pageAllocations.size());
		{
			uint32_t prevAllocs = m_debugResidentAllocations.load(std::memory_order_relaxed);
			m_debugResidentAllocations.store((prevAllocs >= ac) ? (prevAllocs - ac) : 0u, std::memory_order_relaxed);
		}
	}
	DeallocateCLodGroupChunkAllocations(state, groupLocalIndex);

	UploadCLodGroupChunkTable(state);
	return true;
}

void MeshManager::GetCLodActiveUniqueAssetGroupRanges(std::vector<CLodActiveGroupRange>& outRanges, uint32_t& outMaxGroupIndex) const {
	outRanges.clear();
	outMaxGroupIndex = 0u;

	std::unordered_set<uint64_t> seenRanges;
	seenRanges.reserve(m_clodStreamingStateByInstanceIndex.size());

	for (const auto& [_, state] : m_clodStreamingStateByInstanceIndex) {
		if (state.groupCount == 0u) {
			continue;
		}

		const uint64_t key = (static_cast<uint64_t>(state.groupsBase) << 32ull) | static_cast<uint64_t>(state.groupCount);
		if (!seenRanges.insert(key).second) {
			continue;
		}

		CLodActiveGroupRange range{};
		range.groupsBase = state.groupsBase;
		range.groupCount = state.groupCount;
		outRanges.push_back(range);

		const uint32_t rangeEnd = state.groupsBase + state.groupCount;
		outMaxGroupIndex = std::max(outMaxGroupIndex, rangeEnd);
	}
}

void MeshManager::GetCLodCoarsestUniqueAssetGroupRanges(std::vector<CLodActiveGroupRange>& outRanges) const {
	outRanges.clear();

	std::unordered_set<uint64_t> seenRanges;
	seenRanges.reserve(m_clodStreamingStateByInstanceIndex.size());

	for (const auto& [_, state] : m_clodStreamingStateByInstanceIndex) {
		if (state.groupCount == 0u || state.instance == nullptr || state.instance->GetMesh() == nullptr) {
			continue;
		}

		const uint64_t key = (static_cast<uint64_t>(state.groupsBase) << 32ull) | static_cast<uint64_t>(state.groupCount);
		if (!seenRanges.insert(key).second) {
			continue;
		}

		const auto& summary = state.instance->GetMesh()->GetCLodRuntimeSummary();
		if (summary.coarsestRanges.empty()) {
			continue;
		}

		for (const auto& localRange : summary.coarsestRanges) {
			if (localRange.groupCount == 0u || localRange.firstGroup >= state.groupCount) {
				continue;
			}

			const uint32_t clampedCount = std::min<uint32_t>(
				localRange.groupCount,
				state.groupCount - localRange.firstGroup);
			if (clampedCount == 0u) {
				continue;
			}

			CLodActiveGroupRange range{};
			range.groupsBase = state.groupsBase + localRange.firstGroup;
			range.groupCount = clampedCount;
			outRanges.push_back(range);
		}
	}
}

void MeshManager::GetCLodUniqueAssetParentMap(std::vector<int32_t>& outParentGroupByGlobal, uint32_t& outMaxGroupIndex) const {
	outParentGroupByGlobal.clear();
	outMaxGroupIndex = 0u;

	std::unordered_set<uint64_t> seenRanges;
	seenRanges.reserve(m_clodStreamingStateByInstanceIndex.size());

	for (const auto& [_, state] : m_clodStreamingStateByInstanceIndex) {
		if (state.groupCount == 0u || state.instance == nullptr || state.instance->GetMesh() == nullptr) {
			continue;
		}

		const uint64_t key = (static_cast<uint64_t>(state.groupsBase) << 32ull) | static_cast<uint64_t>(state.groupCount);
		if (!seenRanges.insert(key).second) {
			continue;
		}

		const auto& summary = state.instance->GetMesh()->GetCLodRuntimeSummary();
		const uint32_t localGroupCount = std::min<uint32_t>(state.groupCount, static_cast<uint32_t>(summary.parentGroupByLocal.size()));
		if (localGroupCount == 0u) {
			continue;
		}

		const uint32_t rangeEnd = state.groupsBase + localGroupCount;
		outMaxGroupIndex = std::max(outMaxGroupIndex, rangeEnd);
		if (outParentGroupByGlobal.size() < rangeEnd) {
			outParentGroupByGlobal.resize(rangeEnd, -1);
		}

		for (uint32_t groupLocalIndex = 0u; groupLocalIndex < localGroupCount; ++groupLocalIndex) {
			const int32_t parentLocal = summary.parentGroupByLocal[groupLocalIndex];
			if (parentLocal < 0) {
				continue;
			}

			const uint32_t parentLocalU32 = static_cast<uint32_t>(parentLocal);
			if (parentLocalU32 >= localGroupCount) {
				continue;
			}

			const uint32_t parentGlobal = state.groupsBase + parentLocalU32;
			const uint32_t childGlobal = state.groupsBase + groupLocalIndex;
			outParentGroupByGlobal[childGlobal] = static_cast<int32_t>(parentGlobal);
		}
	}
}

void MeshManager::GetCLodStreamingDomainSnapshot(CLodStreamingDomainSnapshot& outSnapshot) const {
	outSnapshot.activeRanges.clear();
	outSnapshot.coarsestRanges.clear();
	outSnapshot.parentGroupByGlobal.clear();
	outSnapshot.groupOriginalErrorByGlobal.clear();
	outSnapshot.maxGroupIndex = 0;

	std::unordered_set<uint64_t> seenRanges;
	seenRanges.reserve(m_clodStreamingStateByInstanceIndex.size());

	for (const auto& [_, state] : m_clodStreamingStateByInstanceIndex) {
		if (state.groupCount == 0u) {
			continue;
		}

		const uint64_t key = (static_cast<uint64_t>(state.groupsBase) << 32ull) | static_cast<uint64_t>(state.groupCount);
		if (!seenRanges.insert(key).second) {
			continue;
		}

		// Active range (same as GetCLodActiveUniqueAssetGroupRanges)
		CLodActiveGroupRange activeRange{};
		activeRange.groupsBase = state.groupsBase;
		activeRange.groupCount = state.groupCount;
		outSnapshot.activeRanges.push_back(activeRange);

		const uint32_t rangeEnd = state.groupsBase + state.groupCount;
		outSnapshot.maxGroupIndex = std::max(outSnapshot.maxGroupIndex, rangeEnd);

		if (state.instance == nullptr || state.instance->GetMesh() == nullptr) {
			continue;
		}

		const auto& summary = state.instance->GetMesh()->GetCLodRuntimeSummary();

		// Coarsest ranges (same as GetCLodCoarsestUniqueAssetGroupRanges)
		for (const auto& localRange : summary.coarsestRanges) {
			if (localRange.groupCount == 0u || localRange.firstGroup >= state.groupCount) {
				continue;
			}
			const uint32_t clampedCount = std::min<uint32_t>(
				localRange.groupCount,
				state.groupCount - localRange.firstGroup);
			if (clampedCount == 0u) {
				continue;
			}
			CLodActiveGroupRange coarsest{};
			coarsest.groupsBase = state.groupsBase + localRange.firstGroup;
			coarsest.groupCount = clampedCount;
			outSnapshot.coarsestRanges.push_back(coarsest);
		}

		// Prefer the parent map cached in the shared streaming state.
		const auto& parentMap = (state.sharedMeshState && !state.sharedMeshState->parentGroupByLocal.empty())
			? state.sharedMeshState->parentGroupByLocal
			: summary.parentGroupByLocal;
		const uint32_t localGroupCount = std::min<uint32_t>(state.groupCount, static_cast<uint32_t>(parentMap.size()));
		if (localGroupCount == 0u) {
			continue;
		}
		const uint32_t parentRangeEnd = state.groupsBase + localGroupCount;
		outSnapshot.maxGroupIndex = std::max(outSnapshot.maxGroupIndex, parentRangeEnd);
		if (outSnapshot.parentGroupByGlobal.size() < parentRangeEnd) {
			outSnapshot.parentGroupByGlobal.resize(parentRangeEnd, -1);
		}
		if (outSnapshot.groupOriginalErrorByGlobal.size() < parentRangeEnd) {
			outSnapshot.groupOriginalErrorByGlobal.resize(parentRangeEnd, 0.0f);
		}
		for (uint32_t groupLocalIndex = 0u; groupLocalIndex < localGroupCount; ++groupLocalIndex) {
			const int32_t parentLocal = parentMap[groupLocalIndex];
			if (parentLocal < 0) {
				continue;
			}
			const uint32_t parentLocalU32 = static_cast<uint32_t>(parentLocal);
			if (parentLocalU32 >= localGroupCount) {
				continue;
			}
			const uint32_t parentGlobal = state.groupsBase + parentLocalU32;
			const uint32_t childGlobal = state.groupsBase + groupLocalIndex;
			outSnapshot.parentGroupByGlobal[childGlobal] = static_cast<int32_t>(parentGlobal);
		}

		// Original error values for residency-driven error override
		const auto& errorMap = (state.sharedMeshState && !state.sharedMeshState->groupErrorByLocal.empty())
			? state.sharedMeshState->groupErrorByLocal
			: summary.groupErrorByLocal;
		const uint32_t errorLocalCount = std::min<uint32_t>(localGroupCount, static_cast<uint32_t>(errorMap.size()));
		for (uint32_t groupLocalIndex = 0u; groupLocalIndex < errorLocalCount; ++groupLocalIndex) {
			const uint32_t globalIndex = state.groupsBase + groupLocalIndex;
			if (globalIndex < outSnapshot.groupOriginalErrorByGlobal.size()) {
				outSnapshot.groupOriginalErrorByGlobal[globalIndex] = errorMap[groupLocalIndex];
			}
		}
	}
}

bool MeshManager::ConsumeCLodStreamingStructureDirty() {
	return m_clodStreamingStructureDirty.exchange(false);
}

void MeshManager::PatchCLodGroupError(uint32_t groupGlobalIndex, float error) {
	// bounds.error is at byte offset 16 within ClusterLODGroup:
	// clodBounds { float center[3]; float radius; float error; }, so error is at offset 16
	constexpr size_t errorFieldOffset = 16;
	const size_t byteOffset = static_cast<size_t>(groupGlobalIndex) * sizeof(ClusterLODGroup) + errorFieldOffset;
	auto handle = m_clusterLODGroups->BeginBulkWrite();
	if (handle.data && byteOffset + sizeof(float) <= handle.capacity) {
		std::memcpy(handle.data + byteOffset, &error, sizeof(float));
		m_clusterLODGroups->EndBulkWrite(byteOffset, sizeof(float));
	}
}

MeshManager::CLodStreamingDebugStats MeshManager::GetCLodStreamingDebugStats() const {
	CLodStreamingDebugStats stats{};
	stats.residentGroups = m_debugResidentGroups.load(std::memory_order_relaxed);
	stats.residentAllocations = m_debugResidentAllocations.load(std::memory_order_relaxed);
	stats.residentAllocationBytes = m_clodPagePool
		? static_cast<uint64_t>(stats.residentAllocations) * m_clodPagePool->GetPageSize()
		: 0ull;
	stats.totalStreamedBytes = m_debugTotalStreamedBytes.load(std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		stats.queuedRequests = static_cast<uint32_t>(m_clodDiskStreamingRequests.size());
	}
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingResultsMutex);
		stats.completedResults = static_cast<uint32_t>(m_clodDiskStreamingResults.size());

		auto getCompletedResultSizeBytes = [](const CLodDiskStreamingResult& result) -> uint64_t {
			uint64_t total = 0;
			for (const auto& blob : result.pageBlobs) {
				total += static_cast<uint64_t>(blob.size());
			}
			return total;
		};
		for (const auto& result : m_clodDiskStreamingResults) {
			stats.completedResultBytes += getCompletedResultSizeBytes(result);
		}
	}

	return stats;
}

void MeshManager::UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data) {
	view->GetBuffer()->UpdateView(view.get(), &data);
}

void MeshManager::UpdatePerMeshInstanceBuffer(std::unique_ptr<BufferView>& view, PerMeshInstanceCB& data) {
	view->GetBuffer()->UpdateView(view.get(), &data);
}

std::shared_ptr<Resource> MeshManager::ProvideResource(ResourceIdentifier const& key) {
	return m_resources[key];
}

std::vector<ResourceIdentifier> MeshManager::GetSupportedKeys() {
	std::vector<ResourceIdentifier> keys;
	keys.reserve(m_resources.size());
	for (auto const& [key, _] : m_resources)
		keys.push_back(key);

	return keys;
}