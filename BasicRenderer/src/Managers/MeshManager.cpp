#include "Managers/MeshManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Mesh/Mesh.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Buffers/BufferView.h"
#include "Mesh/MeshInstance.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Managers/ViewManager.h"
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
	m_perMeshInstanceClodOffsets = DynamicBuffer::CreateShared(sizeof(MeshInstanceClodOffsets), 1, "perMeshInstanceClodOffsets");
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
	m_resources[Builtin::CLod::Groups] = m_clusterLODGroups;
	m_resources[Builtin::CLod::Children] = m_clusterLODChildren;
	//m_resources[Builtin::CLod::Meshlets] = m_clusterLODMeshlets;
	m_resources[Builtin::CLod::MeshletBounds] = m_clusterLODMeshletBounds;
	m_resources[Builtin::CLod::Nodes] = m_clusterLODNodes;

}

void MeshManager::AddMesh(std::shared_ptr<Mesh>& mesh, bool useMeshletReorderedVertices) {
	mesh->SetCurrentMeshManager(this);
	//auto& vertices = useMeshletReorderedVertices ? mesh->GetMeshletReorderedVertices() : mesh->GetVertices();
	//auto& skinningVertices = useMeshletReorderedVertices ? mesh->GetMeshletReorderedSkinningVertices() : mesh->GetSkinningVertices();
	const auto& vertices = mesh->GetStreamingVertices();
	const auto& skinningVertices = mesh->GetStreamingSkinningVertices();
	
	auto numVertices = mesh->GetStreamingNumVertices();
	if (vertices.empty()) {
		// Handle empty vertices case
		throw std::runtime_error("Mesh vertices are empty");
	}

	std::unique_ptr<BufferView> postSkinningView = nullptr;
	std::unique_ptr<BufferView> preSkinningView = nullptr;
	//std::unique_ptr<BufferView> meshletBoundsView = nullptr;
	size_t vertexByteSize = mesh->GetPerMeshCBData().vertexByteSize;
	if (mesh->GetPerMeshCBData().vertexFlags & VertexFlags::VERTEX_SKINNED) {
		unsigned int skinningVertexByteSize = mesh->GetSkinningVertexSize();
		preSkinningView = m_preSkinningVertices->AddData(skinningVertices.data(), numVertices * skinningVertexByteSize, skinningVertexByteSize);
	}
	else {
		postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexByteSize, vertexByteSize);
		//meshletBoundsView = m_meshletBoundsBuffer->AddData(mesh->GetMeshletBounds().data(), mesh->GetMeshletCount() * sizeof(BoundingSphere), sizeof(BoundingSphere));
	}

	// Per mesh buffer
	auto perMeshBufferView = m_perMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), sizeof(PerMeshCB));
	mesh->SetPerMeshBufferView(std::move(perMeshBufferView));

	// Vertex data
	if (preSkinningView) {
		mesh->SetPreSkinningVertexBufferView(std::move(preSkinningView));
	}
	mesh->SetPostSkinningVertexBufferView(std::move(postSkinningView));
	//mesh->SetMeshletBoundsBufferView(std::move(meshletBoundsView));

	// cluster LOD data
	// TODO: Some of this should be in instances, vertex data should go in main vertex buffers
	auto clusterLODGroupsView = m_clusterLODGroups->AddData(mesh->GetCLodGroups().data(), mesh->GetCLodGroups().size() * sizeof(ClusterLODGroup), sizeof(ClusterLODGroup));
	auto clusterLODChildrenView = m_clusterLODChildren->AddData(mesh->GetCLodChildren().data(), mesh->GetCLodChildren().size() * sizeof(ClusterLODChild), sizeof(ClusterLODChild));
	
	// Meshlet offsets
	auto clusterLODMeshletsView = m_meshletOffsets->AddData(mesh->GetCLodMeshlets().data(), mesh->GetCLodMeshlets().size() * sizeof(meshopt_Meshlet), sizeof(meshopt_Meshlet));
	
	// Mesh-local indices into the vertex/index buffer
	auto clusterLODMeshletVerticesView = m_meshletVertexIndices->AddData(mesh->GetCLodMeshletVertices().data(), mesh->GetCLodMeshletVertices().size() * sizeof(uint32_t), sizeof(uint32_t));
	
	// uint8 indices into the vertex index list
	auto clusterLODMeshletTrianglesView = m_meshletTriangles->AddData(mesh->GetCLodMeshletTriangles().data(), mesh->GetCLodMeshletTriangles().size() * sizeof(uint8_t), sizeof(uint8_t));
	
	auto clusterLODMeshletBoundsView = m_clusterLODMeshletBounds->AddData(mesh->GetCLodBounds().data(), mesh->GetCLodBounds().size() * sizeof(BoundingSphere), sizeof(BoundingSphere));
	auto clusterLODNodesView = m_clusterLODNodes->AddData(mesh->GetCLodNodes().data(), mesh->GetCLodNodes().size() * sizeof(ClusterLODNode), sizeof(ClusterLODNode));

	mesh->SetCLodBufferViews( // TODO: cleanup on remove
		std::move(clusterLODGroupsView), 
		std::move(clusterLODChildrenView), 
		std::move(clusterLODMeshletsView), 
		std::move(clusterLODMeshletVerticesView),
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
	auto clusterLODMeshletsView = mesh->GetMesh()->GetCLodMeshletsView();
	auto clusterLODMeshletBoundsView = mesh->GetMesh()->GetCLodMeshletBoundsView();
	auto clusterLODNodesView = mesh->GetMesh()->GetCLodNodesView();

	MeshInstanceClodOffsets clodOffsets = {};
	clodOffsets.groupsBase = static_cast<uint32_t>(clusterLODGroupsView->GetOffset() / sizeof(ClusterLODGroup));
	clodOffsets.childrenBase = static_cast<uint32_t>(clusterLODChildrenView->GetOffset() / sizeof(ClusterLODChild));
	clodOffsets.meshletsBase = static_cast<uint32_t>(clusterLODMeshletsView->GetOffset() / sizeof(meshopt_Meshlet));
	clodOffsets.meshletBoundsBase = static_cast<uint32_t>(clusterLODMeshletBoundsView->GetOffset() / sizeof(BoundingSphere));
	clodOffsets.lodNodesBase = static_cast<uint32_t>(clusterLODNodesView->GetOffset() / sizeof(ClusterLODNode));
	clodOffsets.rootNode = mesh->GetMesh()->GetCLodRootNodeIndex();
	//clodOffsets.rootGroup = mesh->GetMesh()->GetCLodRootGroup();
	auto clodOffsetsView = m_perMeshInstanceClodOffsets->AddData(&clodOffsets, sizeof(MeshInstanceClodOffsets), sizeof(MeshInstanceClodOffsets)); // Indexable by mesh instance

	mesh->SetCLodBufferViews(
		std::move(clodOffsetsView)
	);
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
	mesh->SetCLodBufferViews(nullptr);
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