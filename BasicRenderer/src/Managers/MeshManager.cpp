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
		if (CLodCache::LoadGroupPayload(request.cacheSource, request.groupDiskSpan, payload)) {
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
	for (const auto& chunkView : mesh->GetCLodPreSkinningVertexChunkViews()) {
		if (chunkView) {
			m_preSkinningVertices->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodPostSkinningVertexChunkViews()) {
		if (chunkView) {
			m_postSkinningVertices->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodMeshletVertexChunkViews()) {
		if (chunkView) {
			m_meshletVertexIndices->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodCompressedPositionChunkViews()) {
		if (chunkView) {
			m_clodCompressedPositions->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodCompressedNormalChunkViews()) {
		if (chunkView) {
			m_clodCompressedNormals->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodCompressedMeshletVertexChunkViews()) {
		if (chunkView) {
			m_clodCompressedMeshletVertexIndices->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodMeshletChunkViews()) {
		if (chunkView) {
			m_meshletOffsets->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodMeshletTriangleChunkViews()) {
		if (chunkView) {
			m_meshletTriangles->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodMeshletBoundsChunkViews()) {
		if (chunkView) {
			m_clusterLODMeshletBounds->Deallocate(chunkView.get());
		}
	}
	mesh->SetCLodGroupChunkViews({}, {}, {}, {}, {}, {}, {}, {}, {});
	//auto& vertices = useMeshletReorderedVertices ? mesh->GetMeshletReorderedVertices() : mesh->GetVertices();
	//auto& skinningVertices = useMeshletReorderedVertices ? mesh->GetMeshletReorderedSkinningVertices() : mesh->GetSkinningVertices();
	const auto& vertices = mesh->GetStreamingVertices();
	const auto& skinningVertices = mesh->GetStreamingSkinningVertices();
	const auto& groupVertexChunks = mesh->GetCLodGroupVertexChunks();
	const auto& groupMeshletVertexChunks = mesh->GetCLodGroupMeshletVertexChunks();
	const auto& groupCompressedPositionWordChunks = mesh->GetCLodGroupCompressedPositionWordChunks();
	const auto& groupCompressedNormalWordChunks = mesh->GetCLodGroupCompressedNormalWordChunks();
	const auto& groupCompressedMeshletVertexWordChunks = mesh->GetCLodGroupCompressedMeshletVertexWordChunks();
	const auto& groupMeshletChunks = mesh->GetCLodGroupMeshletChunks();
	const auto& groupMeshletTriangleChunks = mesh->GetCLodGroupMeshletTriangleChunks();
	const auto& groupMeshletBoundsChunks = mesh->GetCLodGroupMeshletBoundsChunks();
	const auto& groupDiskSpans = mesh->GetCLodGroupDiskSpans();
	
	auto numVertices = mesh->GetStreamingNumVertices();
	if (vertices.empty()) {
		// Handle empty vertices case
		throw std::runtime_error("Mesh vertices are empty");
	}

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

	const bool hasDiskBackedGroupChunks = !groupDiskSpans.empty() && (groupDiskSpans.size() == mesh->GetCLodGroupChunks().size());
	const bool hasGroupChunks =
		!groupVertexChunks.empty() &&
		(groupVertexChunks.size() == groupMeshletVertexChunks.size()) &&
		(groupVertexChunks.size() == groupMeshletChunks.size()) &&
		(groupVertexChunks.size() == groupMeshletTriangleChunks.size()) &&
		(groupVertexChunks.size() == groupMeshletBoundsChunks.size());
	const bool hasCompressedGroupChunks =
		hasGroupChunks &&
		(groupVertexChunks.size() == groupCompressedPositionWordChunks.size()) &&
		(groupVertexChunks.size() == groupCompressedNormalWordChunks.size()) &&
		(groupVertexChunks.size() == groupCompressedMeshletVertexWordChunks.size());
	if (mesh->GetPerMeshCBData().vertexFlags & VertexFlags::VERTEX_SKINNED) {
		unsigned int skinningVertexByteSize = mesh->GetSkinningVertexSize();
		preSkinningView = m_preSkinningVertices->AddData(skinningVertices.data(), numVertices * skinningVertexByteSize, skinningVertexByteSize);
	}
	else if (hasGroupChunks || hasDiskBackedGroupChunks) {
		clodPostSkinningChunkViews.reserve(mesh->GetCLodGroupChunks().size());
		for (size_t groupIndex = 0; groupIndex < mesh->GetCLodGroupChunks().size(); ++groupIndex)
		{
			if (groupIndex < groupVertexChunks.size() && !groupVertexChunks[groupIndex].empty()) {
				const auto& groupChunk = groupVertexChunks[groupIndex];
				clodPostSkinningChunkViews.push_back(
					m_postSkinningVertices->AddData(groupChunk.data(), groupChunk.size(), vertexByteSize));
			}
			else {
				clodPostSkinningChunkViews.push_back(nullptr);
			}
		}
	}
	else {
		postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexByteSize, vertexByteSize);
		//meshletBoundsView = m_meshletBoundsBuffer->AddData(mesh->GetMeshletBounds().data(), mesh->GetMeshletCount() * sizeof(BoundingSphere), sizeof(BoundingSphere));
	}

	if (hasGroupChunks || hasDiskBackedGroupChunks)
	{
		const size_t groupCount = mesh->GetCLodGroupChunks().size();
		clodMeshletVertexChunkViews.reserve(groupCount);
		for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
		{
			if (groupIndex < groupMeshletVertexChunks.size() && !groupMeshletVertexChunks[groupIndex].empty()) {
				const auto& meshletChunk = groupMeshletVertexChunks[groupIndex];
				clodMeshletVertexChunkViews.push_back(
					m_meshletVertexIndices->AddData(meshletChunk.data(), meshletChunk.size() * sizeof(uint32_t), sizeof(uint32_t)));
			}
			else {
				clodMeshletVertexChunkViews.push_back(nullptr);
			}
		}

		clodMeshletChunkViews.reserve(groupCount);
		for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
		{
			if (groupIndex < groupMeshletChunks.size() && !groupMeshletChunks[groupIndex].empty()) {
				const auto& meshletChunk = groupMeshletChunks[groupIndex];
				clodMeshletChunkViews.push_back(
					m_meshletOffsets->AddData(meshletChunk.data(), meshletChunk.size() * sizeof(meshopt_Meshlet), sizeof(meshopt_Meshlet)));
			}
			else {
				clodMeshletChunkViews.push_back(nullptr);
			}
		}

		clodMeshletTriangleChunkViews.reserve(groupCount);
		for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
		{
			if (groupIndex < groupMeshletTriangleChunks.size() && !groupMeshletTriangleChunks[groupIndex].empty()) {
				const auto& triangleChunk = groupMeshletTriangleChunks[groupIndex];
				clodMeshletTriangleChunkViews.push_back(
					m_meshletTriangles->AddData(triangleChunk.data(), triangleChunk.size() * sizeof(uint8_t), sizeof(uint8_t)));
			}
			else {
				clodMeshletTriangleChunkViews.push_back(nullptr);
			}
		}

		clodMeshletBoundsChunkViews.reserve(groupCount);
		for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
		{
			if (groupIndex < groupMeshletBoundsChunks.size() && !groupMeshletBoundsChunks[groupIndex].empty()) {
				const auto& boundsChunk = groupMeshletBoundsChunks[groupIndex];
				clodMeshletBoundsChunkViews.push_back(
					m_clusterLODMeshletBounds->AddData(boundsChunk.data(), boundsChunk.size() * sizeof(BoundingSphere), sizeof(BoundingSphere)));
			}
			else {
				clodMeshletBoundsChunkViews.push_back(nullptr);
			}
		}

		if (hasCompressedGroupChunks)
		{
			clodCompressedPositionChunkViews.reserve(groupCompressedPositionWordChunks.size());
			for (const auto& compressedChunk : groupCompressedPositionWordChunks)
			{
				clodCompressedPositionChunkViews.push_back(
					m_clodCompressedPositions->AddData(compressedChunk.data(), compressedChunk.size() * sizeof(uint32_t), sizeof(uint32_t)));
			}

			clodCompressedNormalChunkViews.reserve(groupCompressedNormalWordChunks.size());
			for (const auto& compressedChunk : groupCompressedNormalWordChunks)
			{
				clodCompressedNormalChunkViews.push_back(
					m_clodCompressedNormals->AddData(compressedChunk.data(), compressedChunk.size() * sizeof(uint32_t), sizeof(uint32_t)));
			}

			clodCompressedMeshletVertexChunkViews.reserve(groupCompressedMeshletVertexWordChunks.size());
			for (const auto& compressedChunk : groupCompressedMeshletVertexWordChunks)
			{
				clodCompressedMeshletVertexChunkViews.push_back(
					m_clodCompressedMeshletVertexIndices->AddData(compressedChunk.data(), compressedChunk.size() * sizeof(uint32_t), sizeof(uint32_t)));
			}
		}
		else {
			clodCompressedPositionChunkViews.resize(groupCount);
			clodCompressedNormalChunkViews.resize(groupCount);
			clodCompressedMeshletVertexChunkViews.resize(groupCount);
		}
	}
	else
	{
		throw std::runtime_error("CLod chunk data is required; legacy full-mesh CLod arrays are no longer supported.");
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
	for (const auto& chunkView : mesh->GetCLodPreSkinningVertexChunkViews()) {
		if (chunkView) {
			m_preSkinningVertices->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodPostSkinningVertexChunkViews()) {
		if (chunkView) {
			m_postSkinningVertices->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodMeshletVertexChunkViews()) {
		if (chunkView) {
			m_meshletVertexIndices->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodCompressedPositionChunkViews()) {
		if (chunkView) {
			m_clodCompressedPositions->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodCompressedNormalChunkViews()) {
		if (chunkView) {
			m_clodCompressedNormals->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodCompressedMeshletVertexChunkViews()) {
		if (chunkView) {
			m_clodCompressedMeshletVertexIndices->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodMeshletChunkViews()) {
		if (chunkView) {
			m_meshletOffsets->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodMeshletTriangleChunkViews()) {
		if (chunkView) {
			m_meshletTriangles->Deallocate(chunkView.get());
		}
	}
	for (const auto& chunkView : mesh->GetCLodMeshletBoundsChunkViews()) {
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
	//auto& vertices = useMeshletReorderedVertices ? mesh->GetMesh()->GetMeshletReorderedVertices() : mesh->GetMesh()->GetVertices();
	const auto& vertices = mesh->GetMesh()->GetStreamingVertices();
	auto numVertices = mesh->GetMesh()->GetStreamingNumVertices();

	auto vertexSize = mesh->GetMesh()->GetPerMeshCBData().vertexByteSize;
	unsigned int meshInstanceBufferSize = static_cast<uint32_t>(m_perMeshInstanceBuffers->Size());
	if (mesh->HasSkin()) { // Skinned meshes need unique post-skinning vertex buffers
		auto postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexSize, vertexSize, numVertices * vertexSize * 2); // Allocate twice the size, since we need to ping-pong for motion vectors
		BUFFER_UPLOAD(vertices.data(), numVertices * vertexSize, rg::runtime::UploadTarget::FromShared(postSkinningView->GetBuffer()), postSkinningView->GetOffset() + numVertices * vertexSize);
		auto perMeshInstanceBufferView = m_perMeshInstanceBuffers->AddData(&mesh->GetPerMeshInstanceBufferData(), sizeof(PerMeshInstanceCB), sizeof(PerMeshInstanceCB));
		//auto meshletBoundsBufferView = m_meshletBoundsBuffer->AddData(mesh->GetMesh()->GetMeshletBounds().data(), mesh->GetMesh()->GetMeshletCount() * sizeof(BoundingSphere), sizeof(BoundingSphere));
		mesh->SetBufferViews(std::move(postSkinningView), std::move(perMeshInstanceBufferView));
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

	// This buffer is used for draw call indexing in the visibility buffer, to unpack uint25 visibility data
	//auto clusterIndicesView = m_clusterToVisibleClusterTableIndexBuffer->Allocate(mesh->GetMesh()->GetMeshletCount() * sizeof(unsigned int), sizeof(unsigned int));
	//mesh->SetClusterToVisibleClusterIndicesBufferView(std::move(clusterIndicesView));


	auto clusterLODGroupsView = mesh->GetMesh()->GetCLodGroupsView();
	auto clusterLODChildrenView = mesh->GetMesh()->GetCLodChildrenView();
	auto clusterLODNodesView = mesh->GetMesh()->GetCLodNodesView();
	auto& meshGroupChunks = mesh->GetMesh()->GetCLodGroupChunks();
	auto& meshGroupViews = mesh->GetMesh()->GetCLodPostSkinningVertexChunkViews();
	auto& meshGroupMeshletViews = mesh->GetMesh()->GetCLodMeshletVertexChunkViews();
	auto& meshGroupCompressedPositionViews = mesh->GetMesh()->GetCLodCompressedPositionChunkViews();
	auto& meshGroupCompressedNormalViews = mesh->GetMesh()->GetCLodCompressedNormalChunkViews();
	auto& meshGroupCompressedMeshletVertexViews = mesh->GetMesh()->GetCLodCompressedMeshletVertexChunkViews();
	auto& meshGroupMeshletChunkViews = mesh->GetMesh()->GetCLodMeshletChunkViews();
	auto& meshGroupMeshletTriangleChunkViews = mesh->GetMesh()->GetCLodMeshletTriangleChunkViews();
	auto& meshGroupMeshletBoundsChunkViews = mesh->GetMesh()->GetCLodMeshletBoundsChunkViews();
	std::vector<ClusterLODGroupChunk> instanceGroupChunks(meshGroupChunks.size());
	for (size_t groupIndex = 0; groupIndex < meshGroupChunks.size(); ++groupIndex)
	{
		ClusterLODGroupChunk chunk = meshGroupChunks[groupIndex];
		bool hasRuntimeChunkData = true;
		if (!mesh->HasSkin())
		{
			if (groupIndex < meshGroupViews.size() && meshGroupViews[groupIndex])
			{
				chunk.vertexChunkByteOffset = static_cast<uint32_t>(meshGroupViews[groupIndex]->GetOffset());
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

		if (groupIndex < meshGroupMeshletViews.size() && meshGroupMeshletViews[groupIndex])
		{
			chunk.meshletVerticesBase = static_cast<uint32_t>(meshGroupMeshletViews[groupIndex]->GetOffset() / sizeof(uint32_t));
		}
		else {
			hasRuntimeChunkData = false;
		}
		if (groupIndex < meshGroupMeshletChunkViews.size() && meshGroupMeshletChunkViews[groupIndex])
		{
			chunk.meshletBase = static_cast<uint32_t>(meshGroupMeshletChunkViews[groupIndex]->GetOffset() / sizeof(meshopt_Meshlet));
		}
		else {
			hasRuntimeChunkData = false;
		}
		if (groupIndex < meshGroupMeshletTriangleChunkViews.size() && meshGroupMeshletTriangleChunkViews[groupIndex])
		{
			chunk.meshletTrianglesByteOffset = static_cast<uint32_t>(meshGroupMeshletTriangleChunkViews[groupIndex]->GetOffset());
		}
		else {
			hasRuntimeChunkData = false;
		}
		if (groupIndex < meshGroupMeshletBoundsChunkViews.size() && meshGroupMeshletBoundsChunkViews[groupIndex])
		{
			chunk.meshletBoundsBase = static_cast<uint32_t>(meshGroupMeshletBoundsChunkViews[groupIndex]->GetOffset() / sizeof(BoundingSphere));
		}
		else {
			hasRuntimeChunkData = false;
		}
		if (groupIndex < meshGroupCompressedPositionViews.size() && meshGroupCompressedPositionViews[groupIndex])
		{
			chunk.compressedPositionWordsBase = static_cast<uint32_t>(meshGroupCompressedPositionViews[groupIndex]->GetOffset() / sizeof(uint32_t));
		}
		if (groupIndex < meshGroupCompressedNormalViews.size() && meshGroupCompressedNormalViews[groupIndex])
		{
			chunk.compressedNormalWordsBase = static_cast<uint32_t>(meshGroupCompressedNormalViews[groupIndex]->GetOffset() / sizeof(uint32_t));
		}
		if (groupIndex < meshGroupCompressedMeshletVertexViews.size() && meshGroupCompressedMeshletVertexViews[groupIndex])
		{
			chunk.compressedMeshletVertexWordsBase = static_cast<uint32_t>(meshGroupCompressedMeshletVertexViews[groupIndex]->GetOffset() / sizeof(uint32_t));
		}

		if (!hasRuntimeChunkData) {
			chunk.groupVertexCount = 0;
			chunk.meshletVertexCount = 0;
			chunk.meshletCount = 0;
			chunk.meshletTrianglesByteCount = 0;
			chunk.meshletBoundsCount = 0;
			chunk.compressedPositionWordCount = 0;
			chunk.compressedNormalWordCount = 0;
			chunk.compressedMeshletVertexWordCount = 0;
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
		state.baselineGroupChunks = instanceGroupChunks;
		state.activeGroupChunks = instanceGroupChunks;

		m_clodStreamingStatesByInstanceIndex[state.meshInstanceIndex] = std::move(state);
		m_clodStreamingInstanceLookup[mesh] = static_cast<uint32_t>(mesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
	}
}

void MeshManager::RemoveMeshInstance(MeshInstance* mesh) {

	// Things to remove:
	// - Post-skinning vertices
	// - Per-mesh instance buffer
	// - Meshlet bounds

	auto postSkinningView = mesh->GetPostSkinningVertexBufferView();
	if (postSkinningView != nullptr) {
		m_postSkinningVertices->Deallocate(postSkinningView);
	}
	auto perMeshInstanceBufferView = mesh->GetPerMeshInstanceBufferView();
	if (perMeshInstanceBufferView != nullptr) {
		m_perMeshInstanceBuffers->Deallocate(perMeshInstanceBufferView);
	}
	//auto meshletBoundsView = mesh->GetMeshletBoundsBufferView();
	//if (meshletBoundsView != nullptr) {
	//	m_meshletBoundsBuffer->Deallocate(meshletBoundsView);
	//}
	mesh->SetBufferViews(nullptr, nullptr);
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

	auto& groupVertexViews = mesh->AccessCLodPostSkinningVertexChunkViews();
	auto& meshletVertexViews = mesh->AccessCLodMeshletVertexChunkViews();
	auto& compressedPositionViews = mesh->AccessCLodCompressedPositionChunkViews();
	auto& compressedNormalViews = mesh->AccessCLodCompressedNormalChunkViews();
	auto& compressedMeshletVertexViews = mesh->AccessCLodCompressedMeshletVertexChunkViews();
	auto& meshletViews = mesh->AccessCLodMeshletChunkViews();
	auto& triangleViews = mesh->AccessCLodMeshletTriangleChunkViews();
	auto& boundsViews = mesh->AccessCLodMeshletBoundsChunkViews();
	const auto& sourceChunk = mesh->GetCLodGroupChunks()[groupLocalIndex];

	const bool hasCoreViews = (groupLocalIndex < groupVertexViews.size() && groupVertexViews[groupLocalIndex]
		&& groupLocalIndex < meshletVertexViews.size() && meshletVertexViews[groupLocalIndex]
		&& groupLocalIndex < meshletViews.size() && meshletViews[groupLocalIndex]
		&& groupLocalIndex < triangleViews.size() && triangleViews[groupLocalIndex]
		&& groupLocalIndex < boundsViews.size() && boundsViews[groupLocalIndex]);

	const bool needsCompressed = (sourceChunk.compressedPositionWordCount > 0u)
		|| (sourceChunk.compressedNormalWordCount > 0u)
		|| (sourceChunk.compressedMeshletVertexWordCount > 0u);

	const bool hasCompressedViews = (groupLocalIndex < compressedPositionViews.size() && compressedPositionViews[groupLocalIndex])
		&& (groupLocalIndex < compressedNormalViews.size() && compressedNormalViews[groupLocalIndex])
		&& (groupLocalIndex < compressedMeshletVertexViews.size() && compressedMeshletVertexViews[groupLocalIndex]);

	if (hasCoreViews && (!needsCompressed || hasCompressedViews)) {
		return true;
	}

	if (!mesh->HasCLodDiskStreamingSource()) {
		return false;
	}

	const auto& groupDiskSpans = mesh->GetCLodGroupDiskSpans();
	if (groupLocalIndex >= groupDiskSpans.size()) {
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
		request.groupDiskSpan = groupDiskSpans[groupLocalIndex];
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

		auto& groupVertexViews = mesh->AccessCLodPostSkinningVertexChunkViews();
		auto& meshletVertexViews = mesh->AccessCLodMeshletVertexChunkViews();
		auto& compressedPositionViews = mesh->AccessCLodCompressedPositionChunkViews();
		auto& compressedNormalViews = mesh->AccessCLodCompressedNormalChunkViews();
		auto& compressedMeshletVertexViews = mesh->AccessCLodCompressedMeshletVertexChunkViews();
		auto& meshletViews = mesh->AccessCLodMeshletChunkViews();
		auto& triangleViews = mesh->AccessCLodMeshletTriangleChunkViews();
		auto& boundsViews = mesh->AccessCLodMeshletBoundsChunkViews();

		const size_t requiredSize = mesh->GetCLodGroupChunks().size();
		if (groupVertexViews.size() < requiredSize) groupVertexViews.resize(requiredSize);
		if (meshletVertexViews.size() < requiredSize) meshletVertexViews.resize(requiredSize);
		if (compressedPositionViews.size() < requiredSize) compressedPositionViews.resize(requiredSize);
		if (compressedNormalViews.size() < requiredSize) compressedNormalViews.resize(requiredSize);
		if (compressedMeshletVertexViews.size() < requiredSize) compressedMeshletVertexViews.resize(requiredSize);
		if (meshletViews.size() < requiredSize) meshletViews.resize(requiredSize);
		if (triangleViews.size() < requiredSize) triangleViews.resize(requiredSize);
		if (boundsViews.size() < requiredSize) boundsViews.resize(requiredSize);

		if (!result.vertexChunk.empty()) {
			groupVertexViews[localIndex] = m_postSkinningVertices->AddData(
				result.vertexChunk.data(),
				result.vertexChunk.size(),
				mesh->GetPerMeshCBData().vertexByteSize);
		}
		if (!result.meshletVertexChunk.empty()) {
			meshletVertexViews[localIndex] = m_meshletVertexIndices->AddData(
				result.meshletVertexChunk.data(),
				result.meshletVertexChunk.size() * sizeof(uint32_t),
				sizeof(uint32_t));
		}
		if (!result.compressedPositionWordChunk.empty()) {
			compressedPositionViews[localIndex] = m_clodCompressedPositions->AddData(
				result.compressedPositionWordChunk.data(),
				result.compressedPositionWordChunk.size() * sizeof(uint32_t),
				sizeof(uint32_t));
		}
		if (!result.compressedNormalWordChunk.empty()) {
			compressedNormalViews[localIndex] = m_clodCompressedNormals->AddData(
				result.compressedNormalWordChunk.data(),
				result.compressedNormalWordChunk.size() * sizeof(uint32_t),
				sizeof(uint32_t));
		}
		if (!result.compressedMeshletVertexWordChunk.empty()) {
			compressedMeshletVertexViews[localIndex] = m_clodCompressedMeshletVertexIndices->AddData(
				result.compressedMeshletVertexWordChunk.data(),
				result.compressedMeshletVertexWordChunk.size() * sizeof(uint32_t),
				sizeof(uint32_t));
		}
		if (!result.meshletChunk.empty()) {
			meshletViews[localIndex] = m_meshletOffsets->AddData(
				result.meshletChunk.data(),
				result.meshletChunk.size() * sizeof(meshopt_Meshlet),
				sizeof(meshopt_Meshlet));
		}
		if (!result.meshletTriangleChunk.empty()) {
			triangleViews[localIndex] = m_meshletTriangles->AddData(
				result.meshletTriangleChunk.data(),
				result.meshletTriangleChunk.size() * sizeof(uint8_t),
				sizeof(uint8_t));
		}
		if (!result.meshletBoundsChunk.empty()) {
			boundsViews[localIndex] = m_clusterLODMeshletBounds->AddData(
				result.meshletBoundsChunk.data(),
				result.meshletBoundsChunk.size() * sizeof(BoundingSphere),
				sizeof(BoundingSphere));
		}

		if (localIndex < state.baselineGroupChunks.size()) {
			auto chunk = mesh->GetCLodGroupChunks()[localIndex];
			if (groupVertexViews[localIndex]) chunk.vertexChunkByteOffset = static_cast<uint32_t>(groupVertexViews[localIndex]->GetOffset());
			if (meshletVertexViews[localIndex]) chunk.meshletVerticesBase = static_cast<uint32_t>(meshletVertexViews[localIndex]->GetOffset() / sizeof(uint32_t));
			if (meshletViews[localIndex]) chunk.meshletBase = static_cast<uint32_t>(meshletViews[localIndex]->GetOffset() / sizeof(meshopt_Meshlet));
			if (triangleViews[localIndex]) chunk.meshletTrianglesByteOffset = static_cast<uint32_t>(triangleViews[localIndex]->GetOffset());
			if (boundsViews[localIndex]) chunk.meshletBoundsBase = static_cast<uint32_t>(boundsViews[localIndex]->GetOffset() / sizeof(BoundingSphere));
			if (compressedPositionViews[localIndex]) chunk.compressedPositionWordsBase = static_cast<uint32_t>(compressedPositionViews[localIndex]->GetOffset() / sizeof(uint32_t));
			if (compressedNormalViews[localIndex]) chunk.compressedNormalWordsBase = static_cast<uint32_t>(compressedNormalViews[localIndex]->GetOffset() / sizeof(uint32_t));
			if (compressedMeshletVertexViews[localIndex]) chunk.compressedMeshletVertexWordsBase = static_cast<uint32_t>(compressedMeshletVertexViews[localIndex]->GetOffset() / sizeof(uint32_t));

			state.baselineGroupChunks[localIndex] = chunk;

			const bool currentlyResident = state.activeGroupChunks[localIndex].meshletCount > 0u
				|| state.activeGroupChunks[localIndex].groupVertexCount > 0u;
			if (currentlyResident) {
				state.activeGroupChunks[localIndex] = chunk;
				m_perMeshInstanceClodGroupChunks->UpdateView(state.groupChunksView, state.activeGroupChunks.data());
			}
		}
	}
}

bool MeshManager::ApplyCLodGroupResidency(CLodInstanceStreamingState& state, uint32_t groupLocalIndex, bool resident) {
	if (state.groupChunksView == nullptr || groupLocalIndex >= state.groupCount || groupLocalIndex >= state.baselineGroupChunks.size()) {
		return false;
	}

	ClusterLODGroupChunk desired = state.baselineGroupChunks[groupLocalIndex];
	if (!resident) {
		desired.groupVertexCount = 0;
		desired.meshletVertexCount = 0;
		desired.meshletCount = 0;
		desired.meshletTrianglesByteCount = 0;
		desired.meshletBoundsCount = 0;
	}

	if (groupLocalIndex >= state.activeGroupChunks.size()) {
		return false;
	}

	if (state.activeGroupChunks[groupLocalIndex].groupVertexCount == desired.groupVertexCount
		&& state.activeGroupChunks[groupLocalIndex].meshletVertexCount == desired.meshletVertexCount
		&& state.activeGroupChunks[groupLocalIndex].meshletCount == desired.meshletCount
		&& state.activeGroupChunks[groupLocalIndex].meshletTrianglesByteCount == desired.meshletTrianglesByteCount
		&& state.activeGroupChunks[groupLocalIndex].meshletBoundsCount == desired.meshletBoundsCount
		&& state.activeGroupChunks[groupLocalIndex].compressedPositionWordCount == desired.compressedPositionWordCount
		&& state.activeGroupChunks[groupLocalIndex].compressedNormalWordCount == desired.compressedNormalWordCount
		&& state.activeGroupChunks[groupLocalIndex].compressedMeshletVertexWordCount == desired.compressedMeshletVertexWordCount
		&& state.activeGroupChunks[groupLocalIndex].vertexChunkByteOffset == desired.vertexChunkByteOffset
		&& state.activeGroupChunks[groupLocalIndex].meshletVerticesBase == desired.meshletVerticesBase
		&& state.activeGroupChunks[groupLocalIndex].meshletBase == desired.meshletBase
		&& state.activeGroupChunks[groupLocalIndex].meshletTrianglesByteOffset == desired.meshletTrianglesByteOffset
		&& state.activeGroupChunks[groupLocalIndex].meshletBoundsBase == desired.meshletBoundsBase
		&& state.activeGroupChunks[groupLocalIndex].compressedPositionWordsBase == desired.compressedPositionWordsBase
		&& state.activeGroupChunks[groupLocalIndex].compressedNormalWordsBase == desired.compressedNormalWordsBase
		&& state.activeGroupChunks[groupLocalIndex].compressedMeshletVertexWordsBase == desired.compressedMeshletVertexWordsBase) {
		return true;
	}

	state.activeGroupChunks[groupLocalIndex] = desired;
	m_perMeshInstanceClodGroupChunks->UpdateView(state.groupChunksView, state.activeGroupChunks.data());
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