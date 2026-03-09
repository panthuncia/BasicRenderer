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
	//m_meshletBoundsBuffer = DynamicBuffer::CreateShared(sizeof(BoundingSphere), 1, "meshletBoundsBuffer", false, true);

	//m_clusterToVisibleClusterTableIndexBuffer = DynamicBuffer::CreateShared(sizeof(unsigned int), 1, "clusterIndicesBuffer", false, true);

	m_perMeshBuffers = DynamicBuffer::CreateShared(sizeof(PerMeshCB), 1, "PerMeshBuffers");
	m_perMeshInstanceBuffers = DynamicBuffer::CreateShared(sizeof(PerMeshCB), 1, "perMeshInstanceBuffers");

	// Cluster LOD data
	m_perMeshInstanceClodOffsets = DynamicBuffer::CreateShared(sizeof(MeshInstanceClodOffsets), 10000, "perMeshInstanceClodOffsets");
	m_clodSharedGroupChunks = DynamicBuffer::CreateShared(sizeof(ClusterLODGroupChunk), 10000, "clodSharedGroupChunks");
	m_clodMeshMetadata = DynamicBuffer::CreateShared(sizeof(CLodMeshMetadata), 10000, "clodMeshMetadata");
	m_clusterLODGroups = DynamicBuffer::CreateShared(sizeof(ClusterLODGroup), 10000, "clusterLODGroups");
	m_clusterLODChildren = DynamicBuffer::CreateShared(sizeof(ClusterLODChild), 10000, "clusterLODChildren");
	//m_clusterLODMeshlets = DynamicBuffer::CreateShared(sizeof(meshopt_Meshlet), 1, "clusterLODMeshlets");
	m_clusterLODMeshletBounds = DynamicBuffer::CreateShared(sizeof(BoundingSphere), 10000, "clusterLODMeshletBounds", false, true);
	m_clusterLODNodes = DynamicBuffer::CreateShared(sizeof(ClusterLODNode), 10000, "clusterLODNodes");

	m_postSkinningVertices->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_meshletOffsets->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_meshletVertexIndices->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_meshletTriangles->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_clusterLODMeshletBounds->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);
	m_clodSharedGroupChunks->SetUploadPolicyTag(rg::runtime::UploadPolicyTag::Immediate);

	// Tag resources for memory statistics
	rg::memory::SetResourceUsageHint(*m_preSkinningVertices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_postSkinningVertices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletOffsets, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletVertexIndices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletTriangles, "Mesh Data");
	//m_meshletBoundsBuffer->ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Mesh Data" }));

	//m_clusterToVisibleClusterTableIndexBuffer->ApplyMetadataComponentBundle(EntityComponentBundle().Set<MemoryStatisticsComponents::ResourceUsage>({ "Visibility Buffer Resources" }));

	rg::memory::SetResourceUsageHint(*m_perMeshBuffers, "PerMesh, PerMeshInstance, PerObject");
	rg::memory::SetResourceUsageHint(*m_perMeshInstanceBuffers, "PerMesh, PerMeshInstance, PerObject");


	m_resources[Builtin::PreSkinningVertices] = m_preSkinningVertices;
	m_resources[Builtin::PostSkinningVertices] = m_postSkinningVertices;
	m_resources[Builtin::PerMeshBuffer] = m_perMeshBuffers;
	m_resources[Builtin::PerMeshInstanceBuffer] = m_perMeshInstanceBuffers;
	//m_resources[Builtin::MeshResources::MeshletBounds] = m_meshletBoundsBuffer;
	m_resources[Builtin::MeshResources::MeshletOffsets] = m_meshletOffsets;
	m_resources[Builtin::MeshResources::MeshletVertexIndices] = m_meshletVertexIndices;
	m_resources[Builtin::MeshResources::MeshletTriangles] = m_meshletTriangles;
	//m_resources[Builtin::MeshResources::ClusterToVisibleClusterTableIndexBuffer] = m_clusterToVisibleClusterTableIndexBuffer;

	m_resources[Builtin::CLod::Offsets] = m_perMeshInstanceClodOffsets;
	m_resources[Builtin::CLod::GroupChunks] = m_clodSharedGroupChunks;
	m_resources[Builtin::CLod::MeshMetadata] = m_clodMeshMetadata;
	m_resources[Builtin::CLod::Groups] = m_clusterLODGroups;
	m_resources[Builtin::CLod::Children] = m_clusterLODChildren;
	//m_resources[Builtin::CLod::Meshlets] = m_clusterLODMeshlets;
	m_resources[Builtin::CLod::MeshletBounds] = m_clusterLODMeshletBounds;
	m_resources[Builtin::CLod::Nodes] = m_clusterLODNodes;

	// Page pool
	{
		PagePool::Config ppConfig;
		ppConfig.pageSize  = 65536;              // 64 KB
		ppConfig.slabSize  = 256 * 1024 * 1024;  // 256 MB
		ppConfig.maxSlabs  = 16;
		ppConfig.debugName = "CLodPagePool";
		m_clodPagePool = std::make_unique<PagePool>(ppConfig);
	}
	m_resources[Builtin::CLod::PageTable] = m_clodPagePool->GetPageTableBuffer();
	// Slab buffers are registered dynamically as they're allocated.
	// The PagePoolSlabBase descriptor is resolved per-pass from the first slab.

}

MeshManager::~MeshManager() {
}

void MeshManager::DispatchCLodDiskStreamingBatch() {
	// Drain up to kMaxIoBatchSize requests from the pending queue.
	std::vector<CLodDiskStreamingRequest> batch;
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		const uint32_t toDrain = std::min<uint32_t>(kMaxIoBatchSize, static_cast<uint32_t>(m_clodDiskStreamingRequests.size()));
		if (toDrain == 0u) {
			return;
		}
		batch.reserve(toDrain);
		for (uint32_t i = 0; i < toDrain; ++i) {
			batch.push_back(std::move(m_clodDiskStreamingRequests[i]));
		}
		m_clodDiskStreamingRequests.erase(m_clodDiskStreamingRequests.begin(),
			m_clodDiskStreamingRequests.begin() + toDrain);
	}

	// Pre-allocate results vector (one slot per request, written in parallel).
	std::vector<CLodDiskStreamingResult> results(batch.size());

	// Group requests by container path so each parallel worker can reuse one
	// file handle for all groups from the same container.
	// The lambda captures batch/results by reference and is dispatched over
	// the task scheduler's thread pool (including IO-pinned threads).
	auto& scheduler = TaskSchedulerManager::GetInstance();
	scheduler.ParallelFor(batch.size(), [&batch, &results](size_t i) {
		const auto& request = batch[i];
		auto& result = results[i];
		result.groupGlobalIndex = request.groupGlobalIndex;

		// Thread-local container file handle cache. Each worker thread keeps
		// the last-used container open, avoiding repeated open/close syscalls
		// when consecutive requests target the same file.
		struct TLContainerState {
			std::wstring containerFileName;
			std::string sourceIdentifier;
			std::ifstream file;
			uint32_t groupCount = 0;
			bool valid = false;
		};
		thread_local TLContainerState tls;

		// Reuse if same container, otherwise re-open.
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
			return;
		}

		// Look up the disk locator from the directory embedded in the file.
		const uint64_t directoryEntryOffset =
			static_cast<uint64_t>(sizeof(uint32_t) * 4) // ContainerHeader: magic + version + reserved + groupCount
			+ static_cast<uint64_t>(request.groupLocalIndex) * static_cast<uint64_t>(sizeof(ClusterLODGroupDiskLocator));

		tls.file.seekg(static_cast<std::streamoff>(directoryEntryOffset), std::ios::beg);
		if (!tls.file.good()) {
			result.success = false;
			tls.valid = false; // Stream is bad; force re-open next time.
			return;
		}

		ClusterLODGroupDiskLocator locator{};
		tls.file.read(reinterpret_cast<char*>(&locator), sizeof(locator));
		if (!tls.file.good()) {
			result.success = false;
			tls.valid = false;
			return;
		}

		CLodCache::LoadedGroupPayload payload{};
		if (CLodCache::LoadGroupPayloadDirect(tls.file, locator, payload)) {
			result.groupChunkMetadata = payload.groupChunkMetadata;
			result.vertexChunk = std::move(payload.vertexChunk);
			result.meshletVertexChunk = std::move(payload.meshletVertexChunk);
			result.compressedPositionWordChunk = std::move(payload.compressedPositionWordChunk);
			result.compressedNormalWordChunk = std::move(payload.compressedNormalWordChunk);
			result.compressedMeshletVertexWordChunk = std::move(payload.compressedMeshletVertexWordChunk);
			result.meshletChunk = std::move(payload.meshletChunk);
			result.meshletTriangleChunk = std::move(payload.meshletTriangleChunk);
			result.meshletBoundsChunk = std::move(payload.meshletBoundsChunk);
			result.success = true;
		}
		else {
			result.success = false;
			// If the read failed, the stream might be in a bad state.
			tls.file.clear();
		}
	});

	// Merge results back (single-threaded).
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		for (auto& result : results) {
			m_clodDiskStreamingQueuedGroups.erase(result.groupGlobalIndex);
		}
	}
	{
		std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
		for (auto& result : results) {
			m_clodDiskStreamingResults.push_back(std::move(result));
		}
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
	//std::unique_ptr<BufferView> meshletBoundsView = nullptr;
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
	auto clusterLODChildrenView = m_clusterLODChildren->AddData(mesh->GetCLodChildren().data(), mesh->GetCLodChildren().size() * sizeof(ClusterLODChild), sizeof(ClusterLODChild));
	
	auto clusterLODNodesView = m_clusterLODNodes->AddData(mesh->GetCLodNodes().data(), mesh->GetCLodNodes().size() * sizeof(ClusterLODNode), sizeof(ClusterLODNode));

	mesh->SetCLodBufferViews( // TODO: cleanup on remove
		std::move(clusterLODGroupsView), 
		std::move(clusterLODChildrenView), 
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

	auto clusterLODGroupsView = mesh->GetMesh()->GetCLodGroupsView();
	auto clusterLODChildrenView = mesh->GetMesh()->GetCLodChildrenView();
	auto clusterLODNodesView = mesh->GetMesh()->GetCLodNodesView();
	const uint32_t groupsBase = static_cast<uint32_t>(clusterLODGroupsView->GetOffset() / sizeof(ClusterLODGroup));

	auto meshPtr = mesh->GetMesh().get();
	auto sharedStateIt = m_clodSharedStreamingStateByMesh.find(meshPtr);
	std::shared_ptr<CLodSharedStreamingState> sharedState;
	if (sharedStateIt == m_clodSharedStreamingStateByMesh.end()) {
		const auto& groupChunkHints = mesh->GetMesh()->GetCLodGroupChunkHints();
		std::vector<ClusterLODGroupChunk> baselineGroupChunks(groupChunkHints.size());
		std::vector<ClusterLODGroupChunk> materializedGroupChunks(groupChunkHints.size());
		std::vector<uint8_t> groupResidentFlags(groupChunkHints.size(), 1u);

		for (size_t groupIndex = 0; groupIndex < groupChunkHints.size(); ++groupIndex)
		{
			const uint32_t groupIndexU32 = static_cast<uint32_t>(groupIndex);
			ClusterLODGroupChunk chunk{};
			const auto& hint = groupChunkHints[groupIndex];
			chunk.groupVertexCount = hint.groupVertexCount;
			chunk.meshletVertexCount = hint.meshletVertexCount;
			chunk.meshletCount = hint.meshletCount;
			chunk.meshletTrianglesByteCount = hint.meshletTrianglesByteCount;
			chunk.meshletBoundsCount = hint.meshletBoundsCount;
			chunk.compressedPositionWordCount = hint.compressedPositionWordCount;
			chunk.compressedNormalWordCount = hint.compressedNormalWordCount;
			chunk.compressedMeshletVertexWordCount = hint.compressedMeshletVertexWordCount;

			// All 8 data streams live in the page-pool slab. Groups start
			// non-resident and become resident once streaming populates the
			// slab allocation.
			bool hasRuntimeChunkData = (chunk.pagePoolSlabDescriptorIndex != 0u || chunk.meshletCount == 0u);

			baselineGroupChunks[groupIndex] = chunk;

			if (!hasRuntimeChunkData) {
				chunk.groupVertexCount = 0;
				chunk.meshletVertexCount = 0;
				chunk.meshletCount = 0;
				chunk.meshletTrianglesByteCount = 0;
				chunk.meshletBoundsCount = 0;
				chunk.compressedPositionWordCount = 0;
				chunk.compressedNormalWordCount = 0;
				chunk.compressedMeshletVertexWordCount = 0;
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

		sharedState = std::make_shared<CLodSharedStreamingState>();
		sharedState->mesh = mesh->GetMesh().get();

		CLodMeshMetadata clodMeshMetadata{};
		clodMeshMetadata.groupsBase = groupsBase;
		clodMeshMetadata.childrenBase = static_cast<uint32_t>(clusterLODChildrenView->GetOffset() / sizeof(ClusterLODChild));
		clodMeshMetadata.lodNodesBase = static_cast<uint32_t>(clusterLODNodesView->GetOffset() / sizeof(ClusterLODNode));
		clodMeshMetadata.rootNode = mesh->GetMesh()->GetCLodRootNodeIndex();
		clodMeshMetadata.groupChunkTableBase = (sharedGroupChunksView != nullptr)
			? static_cast<uint32_t>(sharedGroupChunksView->GetOffset() / sizeof(ClusterLODGroupChunk))
			: 0u;
		clodMeshMetadata.groupChunkTableCount = static_cast<uint32_t>(materializedGroupChunks.size());
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
		sharedState->residentGroupAllocations.resize(sharedState->groupCount);

		m_clodSharedStreamingStateByMesh[meshPtr] = sharedState;
		m_clodSharedStreamingRangesDirty = true;
		m_clodStreamingStructureDirty = true;
	}
	else {
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

void MeshManager::ProcessCLodDiskStreamingIO(uint32_t maxCompletedRequests) {
	// First, dispatch a batch of pending IO requests in parallel across the
	// task scheduler's thread pool. This replaces the old single-threaded worker.
	DispatchCLodDiskStreamingBatch();

	// Drain completed results into a local vector under the results lock so the
	// IO thread (once moved to background) can continue pushing results.
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

	// Apply each result under the residency lock and record completions.
	if (!localResults.empty()) {
		std::vector<CLodDiskStreamingCompletion> newCompletions;
		newCompletions.reserve(localResults.size());
		{
			std::lock_guard<std::mutex> residencyLock(m_clodResidencyMutex);
			for (auto& result : localResults) {
				const bool applied = ApplyCompletedCLodDiskStreamingResult(result);
				newCompletions.push_back({ result.groupGlobalIndex, applied });
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

bool MeshManager::QueueCLodDiskStreamingRequest(uint32_t groupGlobalIndex, CLodSharedStreamingState& state, uint32_t groupLocalIndex, bool& outQueued) {
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
	const bool hasRequiredAllocations = residentAllocations.pageAllocation.IsValid()
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
		m_clodDiskStreamingRequests.push_back(std::move(request));
	}

	outQueued = true;
	return false;
}

bool MeshManager::ApplyCompletedCLodDiskStreamingResult(CLodDiskStreamingResult& result) {
	if (!result.success) {
		return false;
	}

	uint32_t localIndex = 0u;
	auto sharedState = FindCLodSharedStreamingStateByGlobalGroup(result.groupGlobalIndex, localIndex);
	if (sharedState == nullptr || sharedState->mesh == nullptr) {
		return false;
	}

	auto* mesh = sharedState->mesh;
	if (localIndex >= sharedState->baselineGroupChunks.size() ||
		localIndex >= sharedState->residentGroupAllocations.size()) {
		return false;
	}

	auto& residentAllocations = sharedState->residentGroupAllocations[localIndex];
	residentAllocations.Reset();

	// Start with baseline chunk and apply any disk-delivered metadata overrides.
	ClusterLODGroupChunk chunk = sharedState->baselineGroupChunks[localIndex];
	if (result.groupChunkMetadata.has_value()) {
		chunk = result.groupChunkMetadata.value();
	}

	// Detect mismatch between metadata-declared meshlet count and
	// the actual data delivered by disk streaming.
	if (chunk.meshletCount > 0u && result.meshletChunk.empty()) {
		spdlog::error(
			"CLod streaming: group {} (local {}) metadata declares meshletCount={} "
			"but loaded meshletChunk is empty — meshlet data will be missing on GPU",
			result.groupGlobalIndex, localIndex, chunk.meshletCount);
		assert(false && "CLod streaming: non-zero meshletCount but empty meshletChunk");
	}

	// Meshlets + bounds are packed into the page-pool blob (streams 7 & 8)
	// — no global StructuredBuffer uploads needed.

	// Pack data streams into a contiguous page-pool blob
	std::vector<std::byte> blob;
	const size_t blobSize = PackGroupPayloadForPagePool(result, mesh, blob, chunk);

	if (blobSize == 0) {
		spdlog::warn("CLod streaming: group {} produced an empty blob", result.groupGlobalIndex);
	}

	// Allocate pages
	const uint64_t pageSize = m_clodPagePool->GetPageSize();
	const uint32_t pagesNeeded = static_cast<uint32_t>((blobSize + pageSize - 1) / pageSize);

	PagePool::PageAllocation pageAlloc;
	if (pagesNeeded > 0) {
		pageAlloc = m_clodPagePool->AllocatePages(pagesNeeded);
		if (!pageAlloc.IsValid()) {
			spdlog::error("CLod streaming: failed to allocate {} pages for group {}",
						  pagesNeeded, result.groupGlobalIndex);
			residentAllocations.Reset();
			return false;
		}
	}

	// Upload blob to slab
	if (blobSize > 0 && pageAlloc.IsValid()) {
		m_clodPagePool->UploadToAllocation(pageAlloc, blob.data(), blobSize);
	}

	// Patch chunk with page-pool addressing
	chunk.pagePoolSlabDescriptorIndex = m_clodPagePool->GetSlabDescriptorIndex(pageAlloc);
	chunk.pagePoolSlabByteOffset = static_cast<uint32_t>(m_clodPagePool->PageToSlabByteOffset(pageAlloc.firstPageID));

	// Store allocation handle for later deallocation.
	residentAllocations.pageAllocation = pageAlloc;

	// Commit chunk updates.
	sharedState->baselineGroupChunks[localIndex] = chunk;
	if (localIndex < sharedState->groupResidentFlags.size()) {
		sharedState->groupResidentFlags[localIndex] = 1u;
	}

	// Debug stats
	{
		m_debugResidentAllocations.fetch_add(1u, std::memory_order_relaxed);
		m_debugResidentAllocationBytes.fetch_add(blobSize, std::memory_order_relaxed);
		m_debugResidentGroups.fetch_add(1u, std::memory_order_relaxed);
	}

	UploadCLodGroupChunkTable(*sharedState);
	return true;
}

void MeshManager::DrainCompletedCLodDiskStreamingGroups(std::vector<CLodDiskStreamingCompletion>& outCompletions) {
	std::lock_guard<std::mutex> resultsLock(m_clodDiskStreamingResultsMutex);
	outCompletions = std::move(m_clodDiskStreamingCompletions);
	m_clodDiskStreamingCompletions.clear();
}

void MeshManager::ZeroCLodGroupChunkCounts(ClusterLODGroupChunk& chunk) {
	chunk.groupVertexCount = 0;
	chunk.meshletVertexCount = 0;
	chunk.meshletCount = 0;
	chunk.meshletTrianglesByteCount = 0;
	chunk.meshletBoundsCount = 0;
	chunk.compressedPositionWordCount = 0;
	chunk.compressedNormalWordCount = 0;
	chunk.compressedMeshletVertexWordCount = 0;
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

	// Free pages for all 8 paged streams.
	m_clodPagePool->FreePages(residentAllocations.pageAllocation);

	// Clear page-pool fields in the baseline chunk so the shader sees zeros.
	if (groupLocalIndex < state.baselineGroupChunks.size()) {
		auto& chunk = state.baselineGroupChunks[groupLocalIndex];
		chunk.pagePoolSlabDescriptorIndex = 0;
		chunk.pagePoolSlabByteOffset = 0;
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

bool MeshManager::ApplyCLodGroupResidency(CLodSharedStreamingState& state, uint32_t groupLocalIndex, bool resident, bool uploadTableImmediately) {
	if (state.groupChunksView == nullptr || groupLocalIndex >= state.groupCount || groupLocalIndex >= state.baselineGroupChunks.size()) {
		return false;
	}

	if (groupLocalIndex >= state.groupResidentFlags.size()) {
		return false;
	}

	if (IsCLodGroupResident(state, groupLocalIndex) == resident) {
		return true;
	}

	if (resident) {
		const auto& chunk = state.baselineGroupChunks[groupLocalIndex];
		const bool invalidChunk =
			(chunk.meshletCount > 0u && chunk.meshletVertexCount == 0u)
			|| (chunk.meshletCount > 0u && chunk.meshletTrianglesByteCount == 0u)
			|| (chunk.meshletCount > 0u && chunk.pagePoolSlabDescriptorIndex == 0u);
		if (invalidChunk) {
			spdlog::error(
				"CLOD residency activation rejected: invalid chunk metadata (groupsBase={}, groupLocalIndex={}, groupGlobalIndex={}, meshletCount={}, meshletVertexCount={}, meshletTrianglesByteCount={}, meshletBoundsCount={}, slabDescriptorIndex={})",
				state.groupsBase,
				groupLocalIndex,
				state.groupsBase + groupLocalIndex,
				chunk.meshletCount,
				chunk.meshletVertexCount,
				chunk.meshletTrianglesByteCount,
				chunk.meshletBoundsCount,
				chunk.pagePoolSlabDescriptorIndex);
			assert(false && "CLOD streamed group chunk metadata invalid on residency activation");
			return false;
		}
	}

	state.groupResidentFlags[groupLocalIndex] = resident ? 1u : 0u;
	if (resident) {
		m_debugResidentGroups.fetch_add(1u, std::memory_order_relaxed);
	} else {
		{
			uint32_t prev = m_debugResidentGroups.load(std::memory_order_relaxed);
			if (prev > 0u) m_debugResidentGroups.store(prev - 1u, std::memory_order_relaxed);
		}
		// Subtract allocation stats before deallocation zeroes the views.
		if (groupLocalIndex < state.residentGroupAllocations.size()) {
			auto& allocs = state.residentGroupAllocations[groupLocalIndex];
			auto countAllocs = [](const CLodSharedStreamingState::ResidentGroupAllocations& a) -> uint32_t {
				return a.pageAllocation.IsValid() ? 1u : 0u;
			};
			auto sumAllocBytes = [](const CLodSharedStreamingState::ResidentGroupAllocations& a) -> uint64_t {
				return 0ull; // All data lives in page pool; byte accounting is in the page allocation itself.
			};
			const uint32_t ac = countAllocs(allocs);
			const uint64_t ab = sumAllocBytes(allocs);
			{
				uint32_t prevAllocs = m_debugResidentAllocations.load(std::memory_order_relaxed);
				m_debugResidentAllocations.store((prevAllocs >= ac) ? (prevAllocs - ac) : 0u, std::memory_order_relaxed);
			}
			{
				uint64_t prevBytes = m_debugResidentAllocationBytes.load(std::memory_order_relaxed);
				m_debugResidentAllocationBytes.store((prevBytes >= ab) ? (prevBytes - ab) : 0ull, std::memory_order_relaxed);
			}
		}
		DeallocateCLodGroupChunkAllocations(state, groupLocalIndex);
	}

	if (uploadTableImmediately) {
		UploadCLodGroupChunkTable(state);
	}
	else {
		state.residencyTableDirty = true;
	}
	return true;
}

void MeshManager::UploadDirtyCLodGroupChunkTables(const std::vector<std::shared_ptr<CLodSharedStreamingState>>& touchedSharedStates) {
	std::unordered_set<CLodSharedStreamingState*> uploaded;
	uploaded.reserve(touchedSharedStates.size());

	for (const auto& sharedState : touchedSharedStates) {
		if (sharedState == nullptr || !sharedState->residencyTableDirty) {
			continue;
		}

		if (!uploaded.insert(sharedState.get()).second) {
			continue;
		}

		UploadCLodGroupChunkTable(*sharedState);
		sharedState->residencyTableDirty = false;
	}
}

MeshManager::CLodGlobalResidencyResult MeshManager::SetCLodGroupResidencyForGlobalEx(uint32_t groupGlobalIndex, bool resident) {
	CLodGlobalResidencyResult result{};

	uint32_t groupLocalIndex = 0u;
	auto sharedState = FindCLodSharedStreamingStateByGlobalGroup(groupGlobalIndex, groupLocalIndex);
	if (sharedState == nullptr) {
		return result;
	}

	if (resident) {
		bool queued = false;
		if (!QueueCLodDiskStreamingRequest(groupGlobalIndex, *sharedState, groupLocalIndex, queued)) {
			if (queued) {
				result.serviced = true;
				result.queued = true;
			}
			return result;
		}
	}

	{
		std::lock_guard<std::mutex> residencyLock(m_clodResidencyMutex);
		result.applied = ApplyCLodGroupResidency(*sharedState, groupLocalIndex, resident, true);
	}
	result.serviced = result.applied;
	return result;
}

uint32_t MeshManager::SetCLodGroupResidencyForGlobal(uint32_t groupGlobalIndex, bool resident) {
	const CLodGlobalResidencyResult result = SetCLodGroupResidencyForGlobalEx(groupGlobalIndex, resident);
	return result.applied ? 1u : 0u;
}

void MeshManager::SetCLodGroupResidencyForGlobalBatchEx(
	const std::vector<CLodGlobalResidencyRequest>& requests,
	std::vector<CLodGlobalResidencyResult>& outResults) {
	outResults.clear();
	outResults.reserve(requests.size());

	if (requests.empty()) {
		return;
	}

	ProcessCLodDiskStreamingIO();

	std::lock_guard<std::mutex> residencyLock(m_clodResidencyMutex);

	std::vector<std::shared_ptr<CLodSharedStreamingState>> touchedSharedStates;
	touchedSharedStates.reserve(requests.size());

	for (const auto& request : requests) {
		CLodGlobalResidencyResult result{};

		uint32_t groupLocalIndex = 0u;
		auto sharedState = FindCLodSharedStreamingStateByGlobalGroup(request.groupGlobalIndex, groupLocalIndex);
		if (sharedState == nullptr) {
			outResults.push_back(result);
			continue;
		}

		if (request.resident) {
			bool queued = false;
			if (!QueueCLodDiskStreamingRequest(request.groupGlobalIndex, *sharedState, groupLocalIndex, queued)) {
				if (queued) {
					result.serviced = true;
					result.queued = true;
				}
				outResults.push_back(result);
				continue;
			}
		}

		result.applied = ApplyCLodGroupResidency(*sharedState, groupLocalIndex, request.resident, false);
		result.serviced = result.applied;
		if (result.applied) {
			touchedSharedStates.push_back(sharedState);
		}

		outResults.push_back(result);
	}

	UploadDirtyCLodGroupChunkTables(touchedSharedStates);
}

void MeshManager::SetCLodGroupResidencyForGlobalBatch(
	const std::vector<CLodGlobalResidencyRequest>& requests,
	std::vector<uint32_t>& outAppliedCounts) {
	outAppliedCounts.clear();
	outAppliedCounts.reserve(requests.size());

	if (requests.empty()) {
		return;
	}

	std::vector<CLodGlobalResidencyResult> results;
	SetCLodGroupResidencyForGlobalBatchEx(requests, results);

	for (const auto& result : results) {
		outAppliedCounts.push_back(result.applied ? 1u : 0u);
	}
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

		// Parent map (same as GetCLodUniqueAssetParentMap)
		const uint32_t localGroupCount = std::min<uint32_t>(state.groupCount, static_cast<uint32_t>(summary.parentGroupByLocal.size()));
		if (localGroupCount == 0u) {
			continue;
		}
		const uint32_t parentRangeEnd = state.groupsBase + localGroupCount;
		outSnapshot.maxGroupIndex = std::max(outSnapshot.maxGroupIndex, parentRangeEnd);
		if (outSnapshot.parentGroupByGlobal.size() < parentRangeEnd) {
			outSnapshot.parentGroupByGlobal.resize(parentRangeEnd, -1);
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
			outSnapshot.parentGroupByGlobal[childGlobal] = static_cast<int32_t>(parentGlobal);
		}
	}
}

bool MeshManager::ConsumeCLodStreamingStructureDirty() {
	return m_clodStreamingStructureDirty.exchange(false);
}

// ── Page-pool streaming helpers ────────────────────────────────────────────

size_t MeshManager::PackGroupPayloadForPagePool(
	const CLodDiskStreamingResult& result,
	const Mesh* mesh,
	std::vector<std::byte>& outBlob,
	ClusterLODGroupChunk& inOutChunk)
{
	// Pack the 8 data streams into a single contiguous blob with 4-byte
	// alignment per stream.
	//
	// Record the byte offset of each stream relative to the start of the blob
	// in the chunk's intra-page-offset fields.

	auto align4 = [](size_t v) -> size_t { return (v + 3u) & ~size_t(3); };

	size_t cursor = 0;

	// Helper: appends raw bytes, records intra-page offset.
	auto appendStream = [&](const void* data, size_t bytes, uint32_t& intraOffsetOut) {
		cursor = align4(cursor);
		intraOffsetOut = static_cast<uint32_t>(cursor);
		if (bytes > 0) {
			outBlob.resize(cursor + bytes);
			std::memcpy(outBlob.data() + cursor, data, bytes);
			cursor += bytes;
		}
	};

	outBlob.clear();

	// 1. Vertex data (byte-addressed, variable stride)
	{
		const size_t bytes = result.vertexChunk.size(); // already raw bytes
		appendStream(result.vertexChunk.data(), bytes, inOutChunk.vertexIntraPageByteOffset);
	}

	// 2. Meshlet vertex indices (uint32_t each)
	{
		const size_t bytes = result.meshletVertexChunk.size() * sizeof(uint32_t);
		appendStream(result.meshletVertexChunk.data(), bytes, inOutChunk.meshletVertexIntraPageByteOffset);
	}

	// 3. Meshlet triangles (uint8_t, byte-addressed)
	{
		const size_t bytes = result.meshletTriangleChunk.size() * sizeof(uint8_t);
		appendStream(result.meshletTriangleChunk.data(), bytes, inOutChunk.triangleIntraPageByteOffset);
	}

	// 4. Compressed positions (uint32_t words)
	{
		const size_t bytes = result.compressedPositionWordChunk.size() * sizeof(uint32_t);
		appendStream(result.compressedPositionWordChunk.data(), bytes, inOutChunk.compPosIntraPageByteOffset);
	}

	// 5. Compressed normals (uint32_t words)
	{
		const size_t bytes = result.compressedNormalWordChunk.size() * sizeof(uint32_t);
		appendStream(result.compressedNormalWordChunk.data(), bytes, inOutChunk.compNormIntraPageByteOffset);
	}

	// 6. Compressed meshlet vertex indices (uint32_t words)
	{
		const size_t bytes = result.compressedMeshletVertexWordChunk.size() * sizeof(uint32_t);
		appendStream(result.compressedMeshletVertexWordChunk.data(), bytes, inOutChunk.compMeshletVertIntraPageByteOffset);
	}

	// 7. Meshlet offsets (meshopt_Meshlet, 16 bytes each)
	{
		const size_t bytes = result.meshletChunk.size() * sizeof(meshopt_Meshlet);
		appendStream(result.meshletChunk.data(), bytes, inOutChunk.meshletIntraPageByteOffset);
	}

	// 8. Meshlet bounds (BoundingSphere, 16 bytes each)
	{
		const size_t bytes = result.meshletBoundsChunk.size() * sizeof(BoundingSphere);
		appendStream(result.meshletBoundsChunk.data(), bytes, inOutChunk.boundsIntraPageByteOffset);
	}

	// Final size (4-byte aligned for good measure).
	outBlob.resize(align4(outBlob.size()));
	return outBlob.size();
}

MeshManager::CLodStreamingDebugStats MeshManager::GetCLodStreamingDebugStats() const {
	CLodStreamingDebugStats stats{};
	stats.residentGroups = m_debugResidentGroups.load(std::memory_order_relaxed);
	stats.residentAllocations = m_debugResidentAllocations.load(std::memory_order_relaxed);
	stats.residentAllocationBytes = m_debugResidentAllocationBytes.load(std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		stats.queuedRequests = static_cast<uint32_t>(m_clodDiskStreamingRequests.size());
	}
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingResultsMutex);
		stats.completedResults = static_cast<uint32_t>(m_clodDiskStreamingResults.size());

		auto getCompletedResultSizeBytes = [](const CLodDiskStreamingResult& result) -> uint64_t {
			return static_cast<uint64_t>(result.vertexChunk.size())
				+ static_cast<uint64_t>(result.meshletVertexChunk.size() * sizeof(uint32_t))
				+ static_cast<uint64_t>(result.compressedPositionWordChunk.size() * sizeof(uint32_t))
				+ static_cast<uint64_t>(result.compressedNormalWordChunk.size() * sizeof(uint32_t))
				+ static_cast<uint64_t>(result.compressedMeshletVertexWordChunk.size() * sizeof(uint32_t))
				+ static_cast<uint64_t>(result.meshletChunk.size() * sizeof(meshopt_Meshlet))
				+ static_cast<uint64_t>(result.meshletTriangleChunk.size() * sizeof(uint8_t))
				+ static_cast<uint64_t>(result.meshletBoundsChunk.size() * sizeof(BoundingSphere));
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