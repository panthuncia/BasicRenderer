#include "Managers/MeshManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Mesh/Mesh.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Buffers/BufferView.h"
#include "Mesh/MeshInstance.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Managers/ViewManager.h"
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
	m_resources[Builtin::CLod::CompressedMeshletVertexIndices] = m_clodCompressedMeshletVertexIndices;
	m_resources[Builtin::CLod::Children] = m_clusterLODChildren;
	//m_resources[Builtin::CLod::Meshlets] = m_clusterLODMeshlets;
	m_resources[Builtin::CLod::MeshletBounds] = m_clusterLODMeshletBounds;
	m_resources[Builtin::CLod::Nodes] = m_clusterLODNodes;

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
	mesh->SetCLodGroupChunkViews({}, {}, {}, {}, {}, {}, {}, {});
	//auto& vertices = useMeshletReorderedVertices ? mesh->GetMeshletReorderedVertices() : mesh->GetVertices();
	//auto& skinningVertices = useMeshletReorderedVertices ? mesh->GetMeshletReorderedSkinningVertices() : mesh->GetSkinningVertices();
	const auto& vertices = mesh->GetStreamingVertices();
	const auto& skinningVertices = mesh->GetStreamingSkinningVertices();
	const auto& groupVertexChunks = mesh->GetCLodGroupVertexChunks();
	const auto& groupMeshletVertexChunks = mesh->GetCLodGroupMeshletVertexChunks();
	const auto& groupCompressedPositionWordChunks = mesh->GetCLodGroupCompressedPositionWordChunks();
	const auto& groupCompressedMeshletVertexWordChunks = mesh->GetCLodGroupCompressedMeshletVertexWordChunks();
	const auto& groupMeshletChunks = mesh->GetCLodGroupMeshletChunks();
	const auto& groupMeshletTriangleChunks = mesh->GetCLodGroupMeshletTriangleChunks();
	const auto& groupMeshletBoundsChunks = mesh->GetCLodGroupMeshletBoundsChunks();
	
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
	std::vector<std::unique_ptr<BufferView>> clodCompressedMeshletVertexChunkViews;
 	std::vector<std::unique_ptr<BufferView>> clodMeshletChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodMeshletTriangleChunkViews;
	std::vector<std::unique_ptr<BufferView>> clodMeshletBoundsChunkViews;

	const bool hasGroupChunks =
		!groupVertexChunks.empty() &&
		(groupVertexChunks.size() == groupMeshletVertexChunks.size()) &&
		(groupVertexChunks.size() == groupMeshletChunks.size()) &&
		(groupVertexChunks.size() == groupMeshletTriangleChunks.size()) &&
		(groupVertexChunks.size() == groupMeshletBoundsChunks.size());
	const bool hasCompressedGroupChunks =
		hasGroupChunks &&
		(groupVertexChunks.size() == groupCompressedPositionWordChunks.size()) &&
		(groupVertexChunks.size() == groupCompressedMeshletVertexWordChunks.size());
	if (mesh->GetPerMeshCBData().vertexFlags & VertexFlags::VERTEX_SKINNED) {
		unsigned int skinningVertexByteSize = mesh->GetSkinningVertexSize();
		preSkinningView = m_preSkinningVertices->AddData(skinningVertices.data(), numVertices * skinningVertexByteSize, skinningVertexByteSize);
	}
	else if (hasGroupChunks) {
		clodPostSkinningChunkViews.reserve(groupVertexChunks.size());
		for (const auto& groupChunk : groupVertexChunks)
		{
			clodPostSkinningChunkViews.push_back(
				m_postSkinningVertices->AddData(groupChunk.data(), groupChunk.size(), vertexByteSize));
		}
	}
	else {
		postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexByteSize, vertexByteSize);
		//meshletBoundsView = m_meshletBoundsBuffer->AddData(mesh->GetMeshletBounds().data(), mesh->GetMeshletCount() * sizeof(BoundingSphere), sizeof(BoundingSphere));
	}

	if (hasGroupChunks)
	{
		clodMeshletVertexChunkViews.reserve(groupMeshletVertexChunks.size());
		for (const auto& meshletChunk : groupMeshletVertexChunks)
		{
			clodMeshletVertexChunkViews.push_back(
				m_meshletVertexIndices->AddData(meshletChunk.data(), meshletChunk.size() * sizeof(uint32_t), sizeof(uint32_t)));
		}

		clodMeshletChunkViews.reserve(groupMeshletChunks.size());
		for (const auto& meshletChunk : groupMeshletChunks)
		{
			clodMeshletChunkViews.push_back(
				m_meshletOffsets->AddData(meshletChunk.data(), meshletChunk.size() * sizeof(meshopt_Meshlet), sizeof(meshopt_Meshlet)));
		}

		clodMeshletTriangleChunkViews.reserve(groupMeshletTriangleChunks.size());
		for (const auto& triangleChunk : groupMeshletTriangleChunks)
		{
			clodMeshletTriangleChunkViews.push_back(
				m_meshletTriangles->AddData(triangleChunk.data(), triangleChunk.size() * sizeof(uint8_t), sizeof(uint8_t)));
		}

		clodMeshletBoundsChunkViews.reserve(groupMeshletBoundsChunks.size());
		for (const auto& boundsChunk : groupMeshletBoundsChunks)
		{
			clodMeshletBoundsChunkViews.push_back(
				m_clusterLODMeshletBounds->AddData(boundsChunk.data(), boundsChunk.size() * sizeof(BoundingSphere), sizeof(BoundingSphere)));
		}

		if (hasCompressedGroupChunks)
		{
			clodCompressedPositionChunkViews.reserve(groupCompressedPositionWordChunks.size());
			for (const auto& compressedChunk : groupCompressedPositionWordChunks)
			{
				clodCompressedPositionChunkViews.push_back(
					m_clodCompressedPositions->AddData(compressedChunk.data(), compressedChunk.size() * sizeof(uint32_t), sizeof(uint32_t)));
			}

			clodCompressedMeshletVertexChunkViews.reserve(groupCompressedMeshletVertexWordChunks.size());
			for (const auto& compressedChunk : groupCompressedMeshletVertexWordChunks)
			{
				clodCompressedMeshletVertexChunkViews.push_back(
					m_clodCompressedMeshletVertexIndices->AddData(compressedChunk.data(), compressedChunk.size() * sizeof(uint32_t), sizeof(uint32_t)));
			}
		}
	}
	else
	{
		clodMeshletVertexChunkViews.push_back(
			m_meshletVertexIndices->AddData(mesh->GetCLodMeshletVertices().data(), mesh->GetCLodMeshletVertices().size() * sizeof(uint32_t), sizeof(uint32_t)));
		clodMeshletChunkViews.push_back(
			m_meshletOffsets->AddData(mesh->GetCLodMeshlets().data(), mesh->GetCLodMeshlets().size() * sizeof(meshopt_Meshlet), sizeof(meshopt_Meshlet)));
		clodMeshletTriangleChunkViews.push_back(
			m_meshletTriangles->AddData(mesh->GetCLodMeshletTriangles().data(), mesh->GetCLodMeshletTriangles().size() * sizeof(uint8_t), sizeof(uint8_t)));
		clodMeshletBoundsChunkViews.push_back(
			m_clusterLODMeshletBounds->AddData(mesh->GetCLodBounds().data(), mesh->GetCLodBounds().size() * sizeof(BoundingSphere), sizeof(BoundingSphere)));
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
		std::move(clodCompressedMeshletVertexChunkViews),
		std::move(clodMeshletChunkViews),
		std::move(clodMeshletTriangleChunkViews),
		std::move(clodMeshletBoundsChunkViews));
	//mesh->SetMeshletBoundsBufferView(std::move(meshletBoundsView));

	// cluster LOD data
	// TODO: Some of this should be in instances, vertex data should go in main vertex buffers
	auto clusterLODGroupsView = m_clusterLODGroups->AddData(mesh->GetCLodGroups().data(), mesh->GetCLodGroups().size() * sizeof(ClusterLODGroup), sizeof(ClusterLODGroup));
	auto clusterLODChildrenView = m_clusterLODChildren->AddData(mesh->GetCLodChildren().data(), mesh->GetCLodChildren().size() * sizeof(ClusterLODChild), sizeof(ClusterLODChild));
	
	// Meshlet offsets
	auto clusterLODMeshletsView = m_meshletOffsets->AddData(mesh->GetCLodMeshlets().data(), mesh->GetCLodMeshlets().size() * sizeof(meshopt_Meshlet), sizeof(meshopt_Meshlet));
	
	// uint8 indices into the vertex index list
	auto clusterLODMeshletTrianglesView = m_meshletTriangles->AddData(mesh->GetCLodMeshletTriangles().data(), mesh->GetCLodMeshletTriangles().size() * sizeof(uint8_t), sizeof(uint8_t));
	
	auto clusterLODMeshletBoundsView = m_clusterLODMeshletBounds->AddData(mesh->GetCLodBounds().data(), mesh->GetCLodBounds().size() * sizeof(BoundingSphere), sizeof(BoundingSphere));
	auto clusterLODNodesView = m_clusterLODNodes->AddData(mesh->GetCLodNodes().data(), mesh->GetCLodNodes().size() * sizeof(ClusterLODNode), sizeof(ClusterLODNode));

	mesh->SetCLodBufferViews( // TODO: cleanup on remove
		std::move(clusterLODGroupsView), 
		std::move(clusterLODChildrenView), 
		std::move(clusterLODMeshletsView), 
		nullptr,
		std::move(clusterLODMeshletTrianglesView),
		std::move(clusterLODMeshletBoundsView),
		std::move(clusterLODNodesView));

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
	auto& meshGroupCompressedMeshletVertexViews = mesh->GetMesh()->GetCLodCompressedMeshletVertexChunkViews();
	auto& meshGroupMeshletChunkViews = mesh->GetMesh()->GetCLodMeshletChunkViews();
	auto& meshGroupMeshletTriangleChunkViews = mesh->GetMesh()->GetCLodMeshletTriangleChunkViews();
	auto& meshGroupMeshletBoundsChunkViews = mesh->GetMesh()->GetCLodMeshletBoundsChunkViews();
	std::vector<ClusterLODGroupChunk> instanceGroupChunks(meshGroupChunks.size());
	for (size_t groupIndex = 0; groupIndex < meshGroupChunks.size(); ++groupIndex)
	{
		ClusterLODGroupChunk chunk = meshGroupChunks[groupIndex];
		if (!mesh->HasSkin())
		{
			if (groupIndex < meshGroupViews.size() && meshGroupViews[groupIndex])
			{
				chunk.vertexChunkByteOffset = static_cast<uint32_t>(meshGroupViews[groupIndex]->GetOffset());
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
		if (groupIndex < meshGroupMeshletChunkViews.size() && meshGroupMeshletChunkViews[groupIndex])
		{
			chunk.meshletBase = static_cast<uint32_t>(meshGroupMeshletChunkViews[groupIndex]->GetOffset() / sizeof(meshopt_Meshlet));
		}
		if (groupIndex < meshGroupMeshletTriangleChunkViews.size() && meshGroupMeshletTriangleChunkViews[groupIndex])
		{
			chunk.meshletTrianglesByteOffset = static_cast<uint32_t>(meshGroupMeshletTriangleChunkViews[groupIndex]->GetOffset());
		}
		if (groupIndex < meshGroupMeshletBoundsChunkViews.size() && meshGroupMeshletBoundsChunkViews[groupIndex])
		{
			chunk.meshletBoundsBase = static_cast<uint32_t>(meshGroupMeshletBoundsChunkViews[groupIndex]->GetOffset() / sizeof(BoundingSphere));
		}
		if (groupIndex < meshGroupCompressedPositionViews.size() && meshGroupCompressedPositionViews[groupIndex])
		{
			chunk.compressedPositionWordsBase = static_cast<uint32_t>(meshGroupCompressedPositionViews[groupIndex]->GetOffset() / sizeof(uint32_t));
		}
		if (groupIndex < meshGroupCompressedMeshletVertexViews.size() && meshGroupCompressedMeshletVertexViews[groupIndex])
		{
			chunk.compressedMeshletVertexWordsBase = static_cast<uint32_t>(meshGroupCompressedMeshletVertexViews[groupIndex]->GetOffset() / sizeof(uint32_t));
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
		&& state.activeGroupChunks[groupLocalIndex].vertexChunkByteOffset == desired.vertexChunkByteOffset
		&& state.activeGroupChunks[groupLocalIndex].meshletVerticesBase == desired.meshletVerticesBase
		&& state.activeGroupChunks[groupLocalIndex].meshletBase == desired.meshletBase
		&& state.activeGroupChunks[groupLocalIndex].meshletTrianglesByteOffset == desired.meshletTrianglesByteOffset
		&& state.activeGroupChunks[groupLocalIndex].meshletBoundsBase == desired.meshletBoundsBase) {
		return true;
	}

	state.activeGroupChunks[groupLocalIndex] = desired;
	m_perMeshInstanceClodGroupChunks->UpdateView(state.groupChunksView, state.activeGroupChunks.data());
	return true;
}

bool MeshManager::SetCLodGroupResidencyForInstance(uint32_t meshInstanceIndex, uint32_t groupGlobalIndex, bool resident) {
	auto it = m_clodStreamingStatesByInstanceIndex.find(meshInstanceIndex);
	if (it == m_clodStreamingStatesByInstanceIndex.end()) {
		return false;
	}

	auto& state = it->second;
	if (groupGlobalIndex < state.groupsBase || groupGlobalIndex >= (state.groupsBase + state.groupCount)) {
		return false;
	}

	const uint32_t groupLocalIndex = groupGlobalIndex - state.groupsBase;
	return ApplyCLodGroupResidency(state, groupLocalIndex, resident);
}

uint32_t MeshManager::SetCLodGroupResidencyForGlobal(uint32_t groupGlobalIndex, bool resident) {
	uint32_t appliedCount = 0;
	for (auto& [_, state] : m_clodStreamingStatesByInstanceIndex) {
		if (groupGlobalIndex < state.groupsBase || groupGlobalIndex >= (state.groupsBase + state.groupCount)) {
			continue;
		}

		const uint32_t groupLocalIndex = groupGlobalIndex - state.groupsBase;
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