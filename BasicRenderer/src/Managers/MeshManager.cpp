#include "Managers/MeshManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Resources/ResourceStates.h"
#include "Mesh/Mesh.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Buffers/BufferView.h"
#include "Mesh/MeshInstance.h"
#include "Resources/Buffers/DynamicBuffer.h"

MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_preSkinningVertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"preSkinnedVertices", true);
	m_postSkinningVertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"PostSkinnedvertices", true, true);
	m_meshletOffsets = resourceManager.CreateIndexedDynamicBuffer(sizeof(meshopt_Meshlet), 1, ResourceState::ALL_SRV, L"meshletOffsets");
	m_meshletIndices = resourceManager.CreateIndexedDynamicBuffer(sizeof(unsigned int), 1, ResourceState::ALL_SRV, L"meshletIndices");
	m_meshletTriangles = resourceManager.CreateIndexedDynamicBuffer(1, 4, ResourceState::ALL_SRV, L"meshletTriangles", true);
	m_resourceGroup = std::make_shared<ResourceGroup>(L"MeshInfo");
	m_resourceGroup->AddResource(m_meshletOffsets);
	m_resourceGroup->AddResource(m_meshletIndices);
	m_resourceGroup->AddResource(m_meshletTriangles);

	m_perMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"PerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
	
	m_perMeshInstanceBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, ResourceState::ALL_SRV, L"perMeshInstanceBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
}

void MeshManager::AddMesh(std::shared_ptr<Mesh>& mesh, MaterialBuckets bucket, bool useMeshletReorderedVertices) {
	mesh->SetCurrentMeshManager(this);
	auto& vertices = useMeshletReorderedVertices ? mesh->GetMeshletReorderedVertices() : mesh->GetVertices();
	auto& skinningVertices = useMeshletReorderedVertices ? mesh->GetMeshletReorderedSkinningVertices() : mesh->GetSkinningVertices();
	auto numVertices = mesh->GetNumVertices(useMeshletReorderedVertices);
    if (vertices.empty()) {
        // Handle empty vertices case
		throw std::runtime_error("Mesh vertices are empty");
    }

	auto& manager = ResourceManager::GetInstance();
	std::unique_ptr<BufferView> postSkinningView = nullptr;
	std::unique_ptr<BufferView> preSkinningView = nullptr;

	size_t vertexByteSize = mesh->GetPerMeshCBData().vertexByteSize;
	if (mesh->GetPerMeshCBData().vertexFlags & VertexFlags::VERTEX_SKINNED) {
		unsigned int skinningVertexByteSize = mesh->GetSkinningVertexSize();
		preSkinningView = m_preSkinningVertices->AddData(skinningVertices.data(), numVertices * skinningVertexByteSize, skinningVertexByteSize);
	}
	else {
		postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexByteSize, vertexByteSize);
	}

	auto& meshlets = mesh->GetMeshlets();
	//auto test = vertices[0];
	spdlog::info("Adding {} meshlets, allocating {} bytes", meshlets.size(), meshlets.size() * sizeof(meshopt_Meshlet));
	auto meshletOffsetsView = m_meshletOffsets->AddData(meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet), sizeof(meshopt_Meshlet));

	auto& meshletVertices = mesh->GetMeshletVertices();
	auto meshletIndicesView = m_meshletIndices->AddData(meshletVertices.data(), meshletVertices.size() * sizeof(unsigned int), sizeof(unsigned int));

	auto& meshletTriangles = mesh->GetMeshletTriangles();
	auto meshletTrianglesView = m_meshletTriangles->AddData(meshletTriangles.data(), meshletTriangles.size() * sizeof(unsigned char), sizeof(unsigned char));

	// Per mesh buffer
	auto perMeshBufferView = m_perMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), sizeof(PerMeshCB));
	mesh->SetPerMeshBufferView(std::move(perMeshBufferView));

	mesh->SetBufferViews(std::move(preSkinningView), std::move(postSkinningView), std::move(meshletOffsetsView), std::move(meshletIndicesView), std::move(meshletTrianglesView));
	mesh->UpdateVertexCount(useMeshletReorderedVertices);
}

void MeshManager::RemoveMesh(Mesh* mesh) {

	// Things to remove:
	// - Meshlet offsets
	// - Meshlet vertices
	// - Meshlet triangles
	// - Pre-skinning vertices, if any
	// - Post-skinning vertices

	auto meshletOffsetsView = mesh->GetMeshletOffsetsBufferView();
	if (meshletOffsetsView != nullptr) {
		m_meshletOffsets->Deallocate(meshletOffsetsView);
	}
	auto meshletVerticesView = mesh->GetMeshletVerticesBufferView();
	if (meshletVerticesView != nullptr) {
		m_meshletIndices->Deallocate(meshletVerticesView);
	}
	auto meshletTrianglesView = mesh->GetMeshletTrianglesBufferView();
	if (meshletTrianglesView != nullptr) {
		m_meshletTriangles->Deallocate(meshletTrianglesView);
	}
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
	mesh->SetBufferViews(nullptr, nullptr, nullptr, nullptr, nullptr);
	mesh->SetCurrentMeshManager(nullptr);
}

void MeshManager::AddMeshInstance(MeshInstance* mesh, bool useMeshletReorderedVertices) {
	mesh->SetCurrentMeshManager(this);
	auto& vertices = useMeshletReorderedVertices ? mesh->GetMesh()->GetMeshletReorderedVertices() : mesh->GetMesh()->GetVertices();
	auto numVertices = mesh->GetMesh()->GetNumVertices(useMeshletReorderedVertices);

	auto vertexSize = mesh->GetMesh()->GetPerMeshCBData().vertexByteSize;
	if (mesh->HasSkin()) { // Skinned meshes need unique post-skinning vertex buffers
		auto postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexSize, vertexSize);
		auto perMeshInstanceBufferView = m_perMeshInstanceBuffers->AddData(&mesh->GetPerMeshInstanceBufferData(), sizeof(PerMeshInstanceCB), sizeof(PerMeshInstanceCB));
		mesh->SetBufferViews(std::move(postSkinningView), std::move(perMeshInstanceBufferView));
	}
	else { // Non-skinned meshes can share post-skinning vertex buffers
		auto perMeshInstanceBufferView = m_perMeshInstanceBuffers->AddData(&mesh->GetPerMeshInstanceBufferData(), sizeof(PerMeshInstanceCB), sizeof(PerMeshInstanceCB));
		mesh->SetBufferViewUsingBaseMesh(std::move(perMeshInstanceBufferView));
	}
}

void MeshManager::RemoveMeshInstance(MeshInstance* mesh) {
	
	// Things to remove:
	// - Post-skinning vertices
	// - Per-mesh instance buffer

	auto postSkinningView = mesh->GetPostSkinningVertexBufferView();
	if (postSkinningView != nullptr) {
		m_postSkinningVertices->Deallocate(postSkinningView);
	}
	auto perMeshInstanceBufferView = mesh->GetPerMeshInstanceBufferView();
	if (perMeshInstanceBufferView != nullptr) {
		m_perMeshInstanceBuffers->Deallocate(perMeshInstanceBufferView);
	}
	mesh->SetBufferViews(nullptr, nullptr);
}

void MeshManager::UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data) {
	view->GetBuffer()->UpdateView(view.get(), &data);
}

void MeshManager::UpdatePerMeshInstanceBuffer(std::unique_ptr<BufferView>& view, PerMeshInstanceCB& data) {
	view->GetBuffer()->UpdateView(view.get(), &data);
}

unsigned int  MeshManager::GetPreSkinningVertexBufferSRVIndex() const {
	return m_preSkinningVertices->GetSRVInfo().index;
}
unsigned int  MeshManager::GetPostSkinningVertexBufferSRVIndex() const {
	return m_postSkinningVertices->GetSRVInfo().index;
}
unsigned int  MeshManager::GetPostSkinningVertexBufferUAVIndex() const {
	return m_postSkinningVertices->GetUAVShaderVisibleInfo().index;
}
unsigned int  MeshManager::GetMeshletOffsetBufferSRVIndex() const {
	return m_meshletOffsets->GetSRVInfo().index;
}
unsigned int  MeshManager::GetMeshletIndexBufferSRVIndex() const {
	return m_meshletIndices->GetSRVInfo().index;
}
unsigned int  MeshManager::GetMeshletTriangleBufferSRVIndex() const {
	return m_meshletTriangles->GetSRVInfo().index;
}
std::shared_ptr<ResourceGroup>  MeshManager::GetResourceGroup() {
	return m_resourceGroup;
}
unsigned int  MeshManager::GetPerMeshBufferSRVIndex() const {
	return m_perMeshBuffers->GetSRVInfo().index;
}
std::shared_ptr<DynamicBuffer>&  MeshManager::GetPerMeshBuffers() {
	return m_perMeshBuffers;
}
std::shared_ptr<DynamicBuffer>&  MeshManager::GetPreSkinningVertices() {
	return m_preSkinningVertices;
}
std::shared_ptr<DynamicBuffer>&  MeshManager::GetPostSkinningVertices() {
	return m_postSkinningVertices;
}
unsigned int  MeshManager::GetPerMeshInstanceBufferSRVIndex() const {
	return m_perMeshInstanceBuffers->GetSRVInfo().index;
}