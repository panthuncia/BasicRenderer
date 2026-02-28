#include "Managers/MeshManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Mesh/Mesh.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Buffers/BufferView.h"
#include "Mesh/MeshInstance.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Managers/ViewManager.h"
#include "Import/CLodCache.h"
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
	m_clodCompressedPositions = DynamicBuffer::CreateShared(sizeof(uint32_t), 1, "clodCompressedPositions");
	m_clodCompressedNormals = DynamicBuffer::CreateShared(sizeof(uint32_t), 1, "clodCompressedNormals");
	m_clodCompressedMeshletVertexIndices = DynamicBuffer::CreateShared(sizeof(uint32_t), 1, "clodCompressedMeshletVertexIndices");
	//m_meshletBoundsBuffer = DynamicBuffer::CreateShared(sizeof(BoundingSphere), 1, "meshletBoundsBuffer", false, true);

	//m_clusterToVisibleClusterTableIndexBuffer = DynamicBuffer::CreateShared(sizeof(unsigned int), 1, "clusterIndicesBuffer", false, true);

	m_perMeshBuffers = DynamicBuffer::CreateShared(sizeof(PerMeshCB), 1, "PerMeshBuffers");
	m_perMeshInstanceBuffers = DynamicBuffer::CreateShared(sizeof(PerMeshCB), 1, "perMeshInstanceBuffers");

	// Cluster LOD data
	m_perMeshInstanceClodOffsets = DynamicBuffer::CreateShared(sizeof(MeshInstanceClodOffsets), 1, "perMeshInstanceClodOffsets");
	m_perMeshInstanceClodGroupChunks = DynamicBuffer::CreateShared(sizeof(ClusterLODGroupChunk), 1, "perMeshInstanceClodGroupChunks");
	m_clusterLODGroups = DynamicBuffer::CreateShared(sizeof(ClusterLODGroup), 1, "clusterLODGroups");
	m_clusterLODChildren = DynamicBuffer::CreateShared(sizeof(ClusterLODChild), 1, "clusterLODChildren");
	//m_clusterLODMeshlets = DynamicBuffer::CreateShared(sizeof(meshopt_Meshlet), 1, "clusterLODMeshlets");
	m_clusterLODMeshletBounds = DynamicBuffer::CreateShared(sizeof(BoundingSphere), 1, "clusterLODMeshletBounds", false, true);
	m_clusterLODNodes = DynamicBuffer::CreateShared(sizeof(ClusterLODNode), 1, "clusterLODNodes");

	// Tag resources for memory statistics
	rg::memory::SetResourceUsageHint(*m_preSkinningVertices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_postSkinningVertices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletOffsets, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletVertexIndices, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_meshletTriangles, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_clodCompressedPositions, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_clodCompressedNormals, "Mesh Data");
	rg::memory::SetResourceUsageHint(*m_clodCompressedMeshletVertexIndices, "Mesh Data");
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
	m_resources[Builtin::CLod::GroupChunks] = m_perMeshInstanceClodGroupChunks;
	m_resources[Builtin::CLod::Groups] = m_clusterLODGroups;
	m_resources[Builtin::CLod::CompressedPositions] = m_clodCompressedPositions;
	m_resources[Builtin::CLod::CompressedNormals] = m_clodCompressedNormals;
	m_resources[Builtin::CLod::CompressedMeshletVertexIndices] = m_clodCompressedMeshletVertexIndices;
	m_resources[Builtin::CLod::Children] = m_clusterLODChildren;
	//m_resources[Builtin::CLod::Meshlets] = m_clusterLODMeshlets;
	m_resources[Builtin::CLod::MeshletBounds] = m_clusterLODMeshletBounds;
	m_resources[Builtin::CLod::Nodes] = m_clusterLODNodes;

	m_clodDiskStreamingThread = std::thread([this]() {
		CLodDiskStreamingWorkerMain();
	});

}

MeshManager::~MeshManager() {
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		m_clodDiskStreamingStop = true;
	}
	m_clodDiskStreamingCv.notify_all();
	if (m_clodDiskStreamingThread.joinable()) {
		m_clodDiskStreamingThread.join();
	}
}

void MeshManager::CLodDiskStreamingWorkerMain() {
	for (;;) {
		CLodDiskStreamingRequest request{};
		{
			std::unique_lock<std::mutex> lock(m_clodDiskStreamingMutex);
			m_clodDiskStreamingCv.wait(lock, [this]() {
				return m_clodDiskStreamingStop || !m_clodDiskStreamingRequests.empty();
			});

			if (m_clodDiskStreamingStop) {
				return;
			}

			request = m_clodDiskStreamingRequests.front();
			m_clodDiskStreamingRequests.pop_front();
		}

		CLodDiskStreamingResult result{};
		result.groupGlobalIndex = request.groupGlobalIndex;
		result.groupLocalIndex = request.groupLocalIndex;

		CLodCache::LoadedGroupPayload payload{};
		if (CLodCache::LoadGroupPayload(request.cacheSource, request.groupLocalIndex, payload)) {
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

		{
			std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
			m_clodDiskStreamingQueuedGroups.erase(request.groupGlobalIndex);
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
	for (const auto& [_, chunkView] : mesh->GetCLodCompressedPositionChunkViews()) {
		if (chunkView) {
			m_clodCompressedPositions->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodCompressedNormalChunkViews()) {
		if (chunkView) {
			m_clodCompressedNormals->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodCompressedMeshletVertexChunkViews()) {
		if (chunkView) {
			m_clodCompressedMeshletVertexIndices->Deallocate(chunkView.get());
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

	const bool hasDiskBackedGroupChunks = !groupDiskLocators.empty() && (groupDiskLocators.size() == mesh->GetCLodGroupChunks().size());
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

	// cluster LOD data
	// TODO: Some of this should be in instances, vertex data should go in main vertex buffers
	auto clusterLODGroupsView = m_clusterLODGroups->AddData(mesh->GetCLodGroups().data(), mesh->GetCLodGroups().size() * sizeof(ClusterLODGroup), sizeof(ClusterLODGroup));
	auto clusterLODChildrenView = m_clusterLODChildren->AddData(mesh->GetCLodChildren().data(), mesh->GetCLodChildren().size() * sizeof(ClusterLODChild), sizeof(ClusterLODChild));
	
	auto clusterLODNodesView = m_clusterLODNodes->AddData(mesh->GetCLodNodes().data(), mesh->GetCLodNodes().size() * sizeof(ClusterLODNode), sizeof(ClusterLODNode));

	mesh->SetCLodBufferViews( // TODO: cleanup on remove
		std::move(clusterLODGroupsView), 
		std::move(clusterLODChildrenView), 
		std::move(clusterLODNodesView));
	mesh->ReleaseCLodChunkUploadData();

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
	for (const auto& [_, chunkView] : mesh->GetCLodCompressedPositionChunkViews()) {
		if (chunkView) {
			m_clodCompressedPositions->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodCompressedNormalChunkViews()) {
		if (chunkView) {
			m_clodCompressedNormals->Deallocate(chunkView.get());
		}
	}
	for (const auto& [_, chunkView] : mesh->GetCLodCompressedMeshletVertexChunkViews()) {
		if (chunkView) {
			m_clodCompressedMeshletVertexIndices->Deallocate(chunkView.get());
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

	auto vertexSize = mesh->GetMesh()->GetPerMeshCBData().vertexByteSize;
	unsigned int meshInstanceBufferSize = static_cast<uint32_t>(m_perMeshInstanceBuffers->Size());
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
	auto& meshGroupChunks = mesh->GetMesh()->GetCLodGroupChunks();
	std::vector<ClusterLODGroupChunk> baselineGroupChunks(meshGroupChunks.size());
	std::vector<ClusterLODGroupChunk> instanceGroupChunks(meshGroupChunks.size());
	std::vector<uint8_t> groupResidentFlags(meshGroupChunks.size(), 1u);
	for (size_t groupIndex = 0; groupIndex < meshGroupChunks.size(); ++groupIndex)
	{
		const uint32_t groupIndexU32 = static_cast<uint32_t>(groupIndex);
		ClusterLODGroupChunk chunk = meshGroupChunks[groupIndex];
		bool hasRuntimeChunkData = true;
		if (!mesh->HasSkin())
		{
			if (const auto* groupVertexView = mesh->GetMesh()->GetCLodPostSkinningVertexChunkView(groupIndexU32); groupVertexView != nullptr)
			{
				chunk.vertexChunkByteOffset = static_cast<uint32_t>(groupVertexView->GetOffset());
			}
			else {
				hasRuntimeChunkData = false;
			}
		}
		else
		{
			const auto& group = mesh->GetMesh()->GetCLodGroups()[groupIndex];
			chunk.vertexChunkByteOffset = mesh->GetPerMeshInstanceBufferData().postSkinningVertexBufferOffset +
				group.firstGroupVertex * mesh->GetMesh()->GetPerMeshCBData().vertexByteSize;
		}

		if (const auto* meshletVertexView = mesh->GetMesh()->GetCLodMeshletVertexChunkView(groupIndexU32); meshletVertexView != nullptr)
		{
			chunk.meshletVerticesBase = static_cast<uint32_t>(meshletVertexView->GetOffset() / sizeof(uint32_t));
		}
		else {
			hasRuntimeChunkData = false;
		}
		if (const auto* meshletView = mesh->GetMesh()->GetCLodMeshletChunkView(groupIndexU32); meshletView != nullptr)
		{
			chunk.meshletBase = static_cast<uint32_t>(meshletView->GetOffset() / sizeof(meshopt_Meshlet));
		}
		else {
			hasRuntimeChunkData = false;
		}
		if (const auto* triangleView = mesh->GetMesh()->GetCLodMeshletTriangleChunkView(groupIndexU32); triangleView != nullptr)
		{
			chunk.meshletTrianglesByteOffset = static_cast<uint32_t>(triangleView->GetOffset());
		}
		else {
			hasRuntimeChunkData = false;
		}
		if (const auto* boundsView = mesh->GetMesh()->GetCLodMeshletBoundsChunkView(groupIndexU32); boundsView != nullptr)
		{
			chunk.meshletBoundsBase = static_cast<uint32_t>(boundsView->GetOffset() / sizeof(BoundingSphere));
		}
		else {
			hasRuntimeChunkData = false;
		}
		if (const auto* compressedPositionView = mesh->GetMesh()->GetCLodCompressedPositionChunkView(groupIndexU32); compressedPositionView != nullptr)
		{
			chunk.compressedPositionWordsBase = static_cast<uint32_t>(compressedPositionView->GetOffset() / sizeof(uint32_t));
		}
		if (const auto* compressedNormalView = mesh->GetMesh()->GetCLodCompressedNormalChunkView(groupIndexU32); compressedNormalView != nullptr)
		{
			chunk.compressedNormalWordsBase = static_cast<uint32_t>(compressedNormalView->GetOffset() / sizeof(uint32_t));
		}
		if (const auto* compressedMeshletVertexView = mesh->GetMesh()->GetCLodCompressedMeshletVertexChunkView(groupIndexU32); compressedMeshletVertexView != nullptr)
		{
			chunk.compressedMeshletVertexWordsBase = static_cast<uint32_t>(compressedMeshletVertexView->GetOffset() / sizeof(uint32_t));
		}

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

		instanceGroupChunks[groupIndex] = chunk;
	}

	std::unique_ptr<BufferView> clodGroupChunksView = nullptr;
	if (!instanceGroupChunks.empty())
	{
		clodGroupChunksView = m_perMeshInstanceClodGroupChunks->AddData(
			instanceGroupChunks.data(),
			instanceGroupChunks.size() * sizeof(ClusterLODGroupChunk),
			sizeof(ClusterLODGroupChunk));
	}

	MeshInstanceClodOffsets clodOffsets = {};
	clodOffsets.groupsBase = static_cast<uint32_t>(clusterLODGroupsView->GetOffset() / sizeof(ClusterLODGroup));
	clodOffsets.childrenBase = static_cast<uint32_t>(clusterLODChildrenView->GetOffset() / sizeof(ClusterLODChild));
	clodOffsets.lodNodesBase = static_cast<uint32_t>(clusterLODNodesView->GetOffset() / sizeof(ClusterLODNode));
	clodOffsets.rootNode = mesh->GetMesh()->GetCLodRootNodeIndex();
	clodOffsets.groupChunkTableBase = (clodGroupChunksView != nullptr)
		? static_cast<uint32_t>(clodGroupChunksView->GetOffset() / sizeof(ClusterLODGroupChunk))
		: 0u;
	clodOffsets.groupChunkTableCount = static_cast<uint32_t>(instanceGroupChunks.size());
	//clodOffsets.rootGroup = mesh->GetMesh()->GetCLodRootGroup();
	auto clodOffsetsView = m_perMeshInstanceClodOffsets->AddData(&clodOffsets, sizeof(MeshInstanceClodOffsets), sizeof(MeshInstanceClodOffsets)); // Indexable by mesh instance

	mesh->SetCLodBufferViews(
		std::move(clodOffsetsView),
		std::move(clodGroupChunksView)
	);

	if (!instanceGroupChunks.empty() && mesh->GetCLodGroupChunksView() != nullptr) {
		CLodInstanceStreamingState state{};
		state.instance = mesh;
		state.meshInstanceIndex = static_cast<uint32_t>(mesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
		state.groupsBase = clodOffsets.groupsBase;
		state.groupCount = clodOffsets.groupChunkTableCount;
		state.groupChunksView = const_cast<BufferView*>(mesh->GetCLodGroupChunksView());
		state.baselineGroupChunks = std::move(baselineGroupChunks);
		state.groupResidentFlags = std::move(groupResidentFlags);

		m_clodStreamingStatesByInstanceIndex[state.meshInstanceIndex] = std::move(state);
		m_clodStreamingInstanceLookup[mesh] = static_cast<uint32_t>(mesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
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
	auto clodGroupChunksView = mesh->GetCLodGroupChunksView();
	if (clodGroupChunksView != nullptr) {
		m_perMeshInstanceClodGroupChunks->Deallocate(clodGroupChunksView);
	}
	mesh->SetCLodBufferViews(nullptr, nullptr);

	auto itLookup = m_clodStreamingInstanceLookup.find(mesh);
	if (itLookup != m_clodStreamingInstanceLookup.end()) {
		m_clodStreamingStatesByInstanceIndex.erase(itLookup->second);
		m_clodStreamingInstanceLookup.erase(itLookup);
	}
}

void MeshManager::ProcessCLodDiskStreamingIO(uint32_t maxCompletedRequests) {
	std::deque<CLodDiskStreamingResult> completed;
	{
		std::lock_guard<std::mutex> lock(m_clodDiskStreamingMutex);
		const uint32_t toDrain = std::min<uint32_t>(maxCompletedRequests, static_cast<uint32_t>(m_clodDiskStreamingResults.size()));
		for (uint32_t i = 0; i < toDrain; ++i) {
			completed.push_back(std::move(m_clodDiskStreamingResults.front()));
			m_clodDiskStreamingResults.pop_front();
		}
	}

	for (auto& result : completed) {
		ApplyCompletedCLodDiskStreamingResult(result);
	}
}

bool MeshManager::QueueCLodDiskStreamingRequest(uint32_t groupGlobalIndex, CLodInstanceStreamingState& state, uint32_t groupLocalIndex) {
	if (state.instance == nullptr || state.instance->GetMesh() == nullptr) {
		return false;
	}

	auto mesh = state.instance->GetMesh();
	if (groupLocalIndex >= mesh->GetCLodGroupChunks().size()) {
		return false;
	}

	const auto* groupVertexView = mesh->GetCLodPostSkinningVertexChunkView(groupLocalIndex);
	const auto* meshletVertexView = mesh->GetCLodMeshletVertexChunkView(groupLocalIndex);
	const auto* compressedPositionView = mesh->GetCLodCompressedPositionChunkView(groupLocalIndex);
	const auto* compressedNormalView = mesh->GetCLodCompressedNormalChunkView(groupLocalIndex);
	const auto* compressedMeshletVertexView = mesh->GetCLodCompressedMeshletVertexChunkView(groupLocalIndex);
	const auto* meshletView = mesh->GetCLodMeshletChunkView(groupLocalIndex);
	const auto* triangleView = mesh->GetCLodMeshletTriangleChunkView(groupLocalIndex);
	const auto* boundsView = mesh->GetCLodMeshletBoundsChunkView(groupLocalIndex);
	const auto& sourceChunk = mesh->GetCLodGroupChunks()[groupLocalIndex];

	const bool hasCoreViews = (groupVertexView != nullptr
		&& meshletVertexView != nullptr
		&& meshletView != nullptr
		&& triangleView != nullptr
		&& boundsView != nullptr);

	const bool needsCompressed = (sourceChunk.compressedPositionWordCount > 0u)
		|| (sourceChunk.compressedNormalWordCount > 0u)
		|| (sourceChunk.compressedMeshletVertexWordCount > 0u);

	const bool hasCompressedViews = (compressedPositionView != nullptr)
		&& (compressedNormalView != nullptr)
		&& (compressedMeshletVertexView != nullptr);

	if (hasCoreViews && (!needsCompressed || hasCompressedViews)) {
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
			return false;
		}

		CLodDiskStreamingRequest request{};
		request.groupGlobalIndex = groupGlobalIndex;
		request.groupLocalIndex = groupLocalIndex;
		request.cacheSource = mesh->GetCLodCacheSource();
		m_clodDiskStreamingRequests.push_back(std::move(request));
	}

	m_clodDiskStreamingCv.notify_one();
	return false;
}

void MeshManager::ApplyCompletedCLodDiskStreamingResult(CLodDiskStreamingResult& result) {
	if (!result.success) {
		return;
	}

	for (auto& [_, state] : m_clodStreamingStatesByInstanceIndex) {
		if (result.groupGlobalIndex < state.groupsBase || result.groupGlobalIndex >= (state.groupsBase + state.groupCount)) {
			continue;
		}
		if (state.instance == nullptr || state.instance->GetMesh() == nullptr) {
			continue;
		}

		auto mesh = state.instance->GetMesh();
		const uint32_t localIndex = result.groupGlobalIndex - state.groupsBase;
		if (localIndex >= mesh->GetCLodGroupChunks().size()) {
			continue;
		}

		if (!result.vertexChunk.empty()) {
			mesh->SetCLodPostSkinningVertexChunkView(localIndex, m_postSkinningVertices->AddData(
				result.vertexChunk.data(),
				result.vertexChunk.size(),
				mesh->GetPerMeshCBData().vertexByteSize));
		}
		if (!result.meshletVertexChunk.empty()) {
			mesh->SetCLodMeshletVertexChunkView(localIndex, m_meshletVertexIndices->AddData(
				result.meshletVertexChunk.data(),
				result.meshletVertexChunk.size() * sizeof(uint32_t),
				sizeof(uint32_t)));
		}
		if (!result.compressedPositionWordChunk.empty()) {
			mesh->SetCLodCompressedPositionChunkView(localIndex, m_clodCompressedPositions->AddData(
				result.compressedPositionWordChunk.data(),
				result.compressedPositionWordChunk.size() * sizeof(uint32_t),
				sizeof(uint32_t)));
		}
		if (!result.compressedNormalWordChunk.empty()) {
			mesh->SetCLodCompressedNormalChunkView(localIndex, m_clodCompressedNormals->AddData(
				result.compressedNormalWordChunk.data(),
				result.compressedNormalWordChunk.size() * sizeof(uint32_t),
				sizeof(uint32_t)));
		}
		if (!result.compressedMeshletVertexWordChunk.empty()) {
			mesh->SetCLodCompressedMeshletVertexChunkView(localIndex, m_clodCompressedMeshletVertexIndices->AddData(
				result.compressedMeshletVertexWordChunk.data(),
				result.compressedMeshletVertexWordChunk.size() * sizeof(uint32_t),
				sizeof(uint32_t)));
		}
		if (!result.meshletChunk.empty()) {
			mesh->SetCLodMeshletChunkView(localIndex, m_meshletOffsets->AddData(
				result.meshletChunk.data(),
				result.meshletChunk.size() * sizeof(meshopt_Meshlet),
				sizeof(meshopt_Meshlet)));
		}
		if (!result.meshletTriangleChunk.empty()) {
			mesh->SetCLodMeshletTriangleChunkView(localIndex, m_meshletTriangles->AddData(
				result.meshletTriangleChunk.data(),
				result.meshletTriangleChunk.size() * sizeof(uint8_t),
				sizeof(uint8_t)));
		}
		if (!result.meshletBoundsChunk.empty()) {
			mesh->SetCLodMeshletBoundsChunkView(localIndex, m_clusterLODMeshletBounds->AddData(
				result.meshletBoundsChunk.data(),
				result.meshletBoundsChunk.size() * sizeof(BoundingSphere),
				sizeof(BoundingSphere)));
		}

		if (localIndex < state.baselineGroupChunks.size()) {
			ClusterLODGroupChunk chunk{};
			if (result.groupChunkMetadata.has_value()) {
				chunk = result.groupChunkMetadata.value();
			}
			else {
				chunk = mesh->GetCLodGroupChunks()[localIndex];
			}
			if (const auto* view = mesh->GetCLodPostSkinningVertexChunkView(localIndex); view != nullptr) chunk.vertexChunkByteOffset = static_cast<uint32_t>(view->GetOffset());
			if (const auto* view = mesh->GetCLodMeshletVertexChunkView(localIndex); view != nullptr) chunk.meshletVerticesBase = static_cast<uint32_t>(view->GetOffset() / sizeof(uint32_t));
			if (const auto* view = mesh->GetCLodMeshletChunkView(localIndex); view != nullptr) chunk.meshletBase = static_cast<uint32_t>(view->GetOffset() / sizeof(meshopt_Meshlet));
			if (const auto* view = mesh->GetCLodMeshletTriangleChunkView(localIndex); view != nullptr) chunk.meshletTrianglesByteOffset = static_cast<uint32_t>(view->GetOffset());
			if (const auto* view = mesh->GetCLodMeshletBoundsChunkView(localIndex); view != nullptr) chunk.meshletBoundsBase = static_cast<uint32_t>(view->GetOffset() / sizeof(BoundingSphere));
			if (const auto* view = mesh->GetCLodCompressedPositionChunkView(localIndex); view != nullptr) chunk.compressedPositionWordsBase = static_cast<uint32_t>(view->GetOffset() / sizeof(uint32_t));
			if (const auto* view = mesh->GetCLodCompressedNormalChunkView(localIndex); view != nullptr) chunk.compressedNormalWordsBase = static_cast<uint32_t>(view->GetOffset() / sizeof(uint32_t));
			if (const auto* view = mesh->GetCLodCompressedMeshletVertexChunkView(localIndex); view != nullptr) chunk.compressedMeshletVertexWordsBase = static_cast<uint32_t>(view->GetOffset() / sizeof(uint32_t));

			if (result.groupChunkMetadata.has_value()) {
				auto& mutableMeshChunks = mesh->AccessCLodGroupChunks();
				if (localIndex < mutableMeshChunks.size()) {
					mutableMeshChunks[localIndex] = chunk;
				}
			}

			state.baselineGroupChunks[localIndex] = chunk;
			if (localIndex < state.groupResidentFlags.size()) {
				state.groupResidentFlags[localIndex] = 1u;
			}
			UploadCLodGroupChunkTable(state);
		}
	}
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

bool MeshManager::IsCLodGroupResident(const CLodInstanceStreamingState& state, uint32_t groupLocalIndex) const {
	if (groupLocalIndex >= state.groupResidentFlags.size()) {
		return false;
	}
	return state.groupResidentFlags[groupLocalIndex] != 0u;
}

bool MeshManager::IsAnyCLodInstanceResidentForGlobalGroup(uint32_t groupGlobalIndex) const {
	for (const auto& [_, state] : m_clodStreamingStatesByInstanceIndex) {
		if (groupGlobalIndex < state.groupsBase || groupGlobalIndex >= (state.groupsBase + state.groupCount)) {
			continue;
		}

		const uint32_t localIndex = groupGlobalIndex - state.groupsBase;
		if (IsCLodGroupResident(state, localIndex)) {
			return true;
		}
	}

	return false;
}

void MeshManager::DeallocateCLodGroupChunkViews(Mesh& mesh, uint32_t groupLocalIndex) {
	if (const auto* view = mesh.GetCLodPostSkinningVertexChunkView(groupLocalIndex); view != nullptr) {
		m_postSkinningVertices->Deallocate(const_cast<BufferView*>(view));
		mesh.SetCLodPostSkinningVertexChunkView(groupLocalIndex, nullptr);
	}
	if (const auto* view = mesh.GetCLodMeshletVertexChunkView(groupLocalIndex); view != nullptr) {
		m_meshletVertexIndices->Deallocate(const_cast<BufferView*>(view));
		mesh.SetCLodMeshletVertexChunkView(groupLocalIndex, nullptr);
	}
	if (const auto* view = mesh.GetCLodCompressedPositionChunkView(groupLocalIndex); view != nullptr) {
		m_clodCompressedPositions->Deallocate(const_cast<BufferView*>(view));
		mesh.SetCLodCompressedPositionChunkView(groupLocalIndex, nullptr);
	}
	if (const auto* view = mesh.GetCLodCompressedNormalChunkView(groupLocalIndex); view != nullptr) {
		m_clodCompressedNormals->Deallocate(const_cast<BufferView*>(view));
		mesh.SetCLodCompressedNormalChunkView(groupLocalIndex, nullptr);
	}
	if (const auto* view = mesh.GetCLodCompressedMeshletVertexChunkView(groupLocalIndex); view != nullptr) {
		m_clodCompressedMeshletVertexIndices->Deallocate(const_cast<BufferView*>(view));
		mesh.SetCLodCompressedMeshletVertexChunkView(groupLocalIndex, nullptr);
	}
	if (const auto* view = mesh.GetCLodMeshletChunkView(groupLocalIndex); view != nullptr) {
		m_meshletOffsets->Deallocate(const_cast<BufferView*>(view));
		mesh.SetCLodMeshletChunkView(groupLocalIndex, nullptr);
	}
	if (const auto* view = mesh.GetCLodMeshletTriangleChunkView(groupLocalIndex); view != nullptr) {
		m_meshletTriangles->Deallocate(const_cast<BufferView*>(view));
		mesh.SetCLodMeshletTriangleChunkView(groupLocalIndex, nullptr);
	}
	if (const auto* view = mesh.GetCLodMeshletBoundsChunkView(groupLocalIndex); view != nullptr) {
		m_clusterLODMeshletBounds->Deallocate(const_cast<BufferView*>(view));
		mesh.SetCLodMeshletBoundsChunkView(groupLocalIndex, nullptr);
	}
}

void MeshManager::UploadCLodGroupChunkTable(const CLodInstanceStreamingState& state) {
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

	m_perMeshInstanceClodGroupChunks->UpdateView(state.groupChunksView, materializedGroupChunks.data());
}

bool MeshManager::ApplyCLodGroupResidency(CLodInstanceStreamingState& state, uint32_t groupLocalIndex, bool resident) {
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
			|| (chunk.meshletBoundsCount > 0u && chunk.meshletBase == 0u && chunk.meshletCount > 0u);
		if (invalidChunk) {
			spdlog::error(
				"CLOD residency activation rejected: invalid chunk metadata (instanceIndex={}, groupLocalIndex={}, groupGlobalIndex={}, meshletCount={}, meshletVertexCount={}, meshletTrianglesByteCount={}, meshletBoundsCount={})",
				state.meshInstanceIndex,
				groupLocalIndex,
				state.groupsBase + groupLocalIndex,
				chunk.meshletCount,
				chunk.meshletVertexCount,
				chunk.meshletTrianglesByteCount,
				chunk.meshletBoundsCount);
			assert(false && "CLOD streamed group chunk metadata invalid on residency activation");
			return false;
		}
	}

	state.groupResidentFlags[groupLocalIndex] = resident ? 1u : 0u;
	if (!resident) {
		const uint32_t groupGlobalIndex = state.groupsBase + groupLocalIndex;
		if (!IsAnyCLodInstanceResidentForGlobalGroup(groupGlobalIndex) && state.instance != nullptr && state.instance->GetMesh() != nullptr) {
			DeallocateCLodGroupChunkViews(*state.instance->GetMesh(), groupLocalIndex);
		}
	}
	UploadCLodGroupChunkTable(state);
	return true;
}

bool MeshManager::SetCLodGroupResidencyForInstance(uint32_t meshInstanceIndex, uint32_t groupGlobalIndex, bool resident) {
	ProcessCLodDiskStreamingIO();

	auto it = m_clodStreamingStatesByInstanceIndex.find(meshInstanceIndex);
	if (it == m_clodStreamingStatesByInstanceIndex.end()) {
		return false;
	}

	auto& state = it->second;
	if (groupGlobalIndex < state.groupsBase || groupGlobalIndex >= (state.groupsBase + state.groupCount)) {
		return false;
	}

	const uint32_t groupLocalIndex = groupGlobalIndex - state.groupsBase;
	if (resident && !QueueCLodDiskStreamingRequest(groupGlobalIndex, state, groupLocalIndex)) {
		return false;
	}
	return ApplyCLodGroupResidency(state, groupLocalIndex, resident);
}

uint32_t MeshManager::SetCLodGroupResidencyForGlobal(uint32_t groupGlobalIndex, bool resident) {
	ProcessCLodDiskStreamingIO();

	uint32_t appliedCount = 0;
	for (auto& [_, state] : m_clodStreamingStatesByInstanceIndex) {
		if (groupGlobalIndex < state.groupsBase || groupGlobalIndex >= (state.groupsBase + state.groupCount)) {
			continue;
		}

		const uint32_t groupLocalIndex = groupGlobalIndex - state.groupsBase;
		if (resident && !QueueCLodDiskStreamingRequest(groupGlobalIndex, state, groupLocalIndex)) {
			continue;
		}
		if (ApplyCLodGroupResidency(state, groupLocalIndex, resident)) {
			appliedCount++;
		}
	}

	return appliedCount;
}

void MeshManager::GetCLodActiveUniqueAssetGroupRanges(std::vector<CLodActiveGroupRange>& outRanges, uint32_t& outMaxGroupIndex) const {
	outRanges.clear();
	outMaxGroupIndex = 0u;

	std::unordered_set<uint64_t> seenRanges;
	seenRanges.reserve(m_clodStreamingStatesByInstanceIndex.size());

	for (const auto& [_, state] : m_clodStreamingStatesByInstanceIndex) {
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
	seenRanges.reserve(m_clodStreamingStatesByInstanceIndex.size());

	for (const auto& [_, state] : m_clodStreamingStatesByInstanceIndex) {
		if (state.groupCount == 0u || state.instance == nullptr || state.instance->GetMesh() == nullptr) {
			continue;
		}

		const uint64_t key = (static_cast<uint64_t>(state.groupsBase) << 32ull) | static_cast<uint64_t>(state.groupCount);
		if (!seenRanges.insert(key).second) {
			continue;
		}

		const auto& groups = state.instance->GetMesh()->GetCLodGroups();
		const uint32_t localGroupCount = std::min<uint32_t>(state.groupCount, static_cast<uint32_t>(groups.size()));
		if (localGroupCount == 0u) {
			continue;
		}

		int32_t coarsestDepth = groups[0].depth;
		for (uint32_t groupLocalIndex = 1u; groupLocalIndex < localGroupCount; ++groupLocalIndex) {
			coarsestDepth = std::max(coarsestDepth, groups[groupLocalIndex].depth);
		}

		uint32_t runStart = std::numeric_limits<uint32_t>::max();
		for (uint32_t groupLocalIndex = 0u; groupLocalIndex < localGroupCount; ++groupLocalIndex) {
			const bool isCoarsest = groups[groupLocalIndex].depth == coarsestDepth;
			if (isCoarsest) {
				if (runStart == std::numeric_limits<uint32_t>::max()) {
					runStart = groupLocalIndex;
				}
				continue;
			}

			if (runStart != std::numeric_limits<uint32_t>::max()) {
				CLodActiveGroupRange range{};
				range.groupsBase = state.groupsBase + runStart;
				range.groupCount = groupLocalIndex - runStart;
				outRanges.push_back(range);
				runStart = std::numeric_limits<uint32_t>::max();
			}
		}

		if (runStart != std::numeric_limits<uint32_t>::max()) {
			CLodActiveGroupRange range{};
			range.groupsBase = state.groupsBase + runStart;
			range.groupCount = localGroupCount - runStart;
			outRanges.push_back(range);
		}
	}
}

void MeshManager::GetCLodUniqueAssetParentMap(std::vector<int32_t>& outParentGroupByGlobal, uint32_t& outMaxGroupIndex) const {
	outParentGroupByGlobal.clear();
	outMaxGroupIndex = 0u;

	std::unordered_set<uint64_t> seenRanges;
	seenRanges.reserve(m_clodStreamingStatesByInstanceIndex.size());

	for (const auto& [_, state] : m_clodStreamingStatesByInstanceIndex) {
		if (state.groupCount == 0u || state.instance == nullptr || state.instance->GetMesh() == nullptr) {
			continue;
		}

		const uint64_t key = (static_cast<uint64_t>(state.groupsBase) << 32ull) | static_cast<uint64_t>(state.groupCount);
		if (!seenRanges.insert(key).second) {
			continue;
		}

		const auto& groups = state.instance->GetMesh()->GetCLodGroups();
		const auto& children = state.instance->GetMesh()->GetCLodChildren();
		const uint32_t localGroupCount = std::min<uint32_t>(state.groupCount, static_cast<uint32_t>(groups.size()));
		if (localGroupCount == 0u) {
			continue;
		}

		const uint32_t rangeEnd = state.groupsBase + localGroupCount;
		outMaxGroupIndex = std::max(outMaxGroupIndex, rangeEnd);
		if (outParentGroupByGlobal.size() < rangeEnd) {
			outParentGroupByGlobal.resize(rangeEnd, -1);
		}

		for (uint32_t groupLocalIndex = 0u; groupLocalIndex < localGroupCount; ++groupLocalIndex) {
			const auto& group = groups[groupLocalIndex];
			const uint32_t childBegin = group.firstChild;
			const uint32_t childEnd = std::min<uint32_t>(childBegin + group.childCount, static_cast<uint32_t>(children.size()));
			for (uint32_t childIndex = childBegin; childIndex < childEnd; ++childIndex) {
				const int32_t refinedGroupLocal = children[childIndex].refinedGroup;
				if (refinedGroupLocal < 0) {
					continue;
				}

				const uint32_t refinedGroupLocalU32 = static_cast<uint32_t>(refinedGroupLocal);
				if (refinedGroupLocalU32 >= localGroupCount) {
					continue;
				}

				const uint32_t parentGlobal = state.groupsBase + groupLocalIndex;
				const uint32_t childGlobal = state.groupsBase + refinedGroupLocalU32;
				outParentGroupByGlobal[childGlobal] = static_cast<int32_t>(parentGlobal);
			}
		}
	}
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