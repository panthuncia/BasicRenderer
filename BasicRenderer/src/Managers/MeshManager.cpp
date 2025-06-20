#include "Managers/MeshManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Resources/ResourceStates.h"
#include "Mesh/Mesh.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Buffers/BufferView.h"
#include "Mesh/MeshInstance.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Managers/CameraManager.h"
#include "../../generated/BuiltinResources.h"

MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_preSkinningVertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, L"preSkinnedVertices", true);
	m_postSkinningVertices = resourceManager.CreateIndexedDynamicBuffer(1, 4, L"PostSkinnedvertices", true, true);
	m_meshletOffsets = resourceManager.CreateIndexedDynamicBuffer(sizeof(meshopt_Meshlet), 1, L"meshletOffsets");
	m_meshletVertexIndices = resourceManager.CreateIndexedDynamicBuffer(sizeof(unsigned int), 1, L"meshletVertexIndices");
	m_meshletTriangles = resourceManager.CreateIndexedDynamicBuffer(1, 4, L"meshletTriangles", true);
	m_meshletBoundsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(BoundingSphere), 1, L"meshletBoundsBuffer", false, true);
	m_meshletBitfieldBuffer = resourceManager.CreateIndexedDynamicBuffer(1, 4, L"meshletBitfieldBuffer", true, true);

	m_perMeshBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, L"PerMeshBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);
	
	m_perMeshInstanceBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerMeshCB), 1, L"perMeshInstanceBuffers");//resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerMeshCB>(ResourceState::ALL_SRV, 1, L"perMeshBuffers<PerMeshCB>", 1);

	m_resources[Builtin::PreSkinningVertices] = m_preSkinningVertices;
	m_resources[Builtin::PostSkinningVertices] = m_postSkinningVertices;
	m_resources[Builtin::PerMeshBuffer] = m_perMeshBuffers;
	m_resources[Builtin::PerMeshInstanceBuffer] = m_perMeshInstanceBuffers;
	m_resources[Builtin::MeshResources::MeshletBounds] = m_meshletBoundsBuffer;
	m_resources[Builtin::MeshResources::MeshletOffsets] = m_meshletOffsets;
	m_resources[Builtin::MeshResources::MeshletVertexIndices] = m_meshletVertexIndices;
	m_resources[Builtin::MeshResources::MeshletTriangles] = m_meshletTriangles;
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
	std::unique_ptr<BufferView> meshletBoundsView = nullptr;
	size_t vertexByteSize = mesh->GetPerMeshCBData().vertexByteSize;
	if (mesh->GetPerMeshCBData().vertexFlags & VertexFlags::VERTEX_SKINNED) {
		unsigned int skinningVertexByteSize = mesh->GetSkinningVertexSize();
		preSkinningView = m_preSkinningVertices->AddData(skinningVertices.data(), numVertices * skinningVertexByteSize, skinningVertexByteSize);
	}
	else {
		postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexByteSize, vertexByteSize);
		meshletBoundsView = m_meshletBoundsBuffer->AddData(mesh->GetMeshletBounds().data(), mesh->GetMeshletCount() * sizeof(BoundingSphere), sizeof(BoundingSphere));
	}

	auto& meshlets = mesh->GetMeshlets();
	//auto test = vertices[0];
	spdlog::info("Adding {} meshlets, allocating {} bytes", meshlets.size(), meshlets.size() * sizeof(meshopt_Meshlet));
	auto meshletOffsetsView = m_meshletOffsets->AddData(meshlets.data(), meshlets.size() * sizeof(meshopt_Meshlet), sizeof(meshopt_Meshlet));

	auto& meshletVertices = mesh->GetMeshletVertices();
	auto meshletVertexIndicesView = m_meshletVertexIndices->AddData(meshletVertices.data(), meshletVertices.size() * sizeof(unsigned int), sizeof(unsigned int));

	auto& meshletTriangles = mesh->GetMeshletTriangles();
	auto meshletTrianglesView = m_meshletTriangles->AddData(meshletTriangles.data(), meshletTriangles.size() * sizeof(unsigned char), sizeof(unsigned char));

	// Per mesh buffer
	auto perMeshBufferView = m_perMeshBuffers->AddData(&mesh->GetPerMeshCBData(), sizeof(PerMeshCB), sizeof(PerMeshCB));
	mesh->SetPerMeshBufferView(std::move(perMeshBufferView));

	mesh->SetBufferViews(std::move(preSkinningView), std::move(postSkinningView), std::move(meshletOffsetsView), std::move(meshletVertexIndicesView), std::move(meshletTrianglesView), std::move(meshletBoundsView));
	mesh->UpdateVertexCount(useMeshletReorderedVertices);
}

void MeshManager::RemoveMesh(Mesh* mesh) {

	// Things to remove:
	// - Meshlet offsets
	// - Meshlet vertices
	// - Meshlet triangles
	// - Pre-skinning vertices, if any
	// - Post-skinning vertices
	// - Meshlet bounds

	auto meshletOffsetsView = mesh->GetMeshletOffsetsBufferView();
	if (meshletOffsetsView != nullptr) {
		m_meshletOffsets->Deallocate(meshletOffsetsView);
	}
	auto meshletVerticesView = mesh->GetMeshletVerticesBufferView();
	if (meshletVerticesView != nullptr) { // TODO: I think this is a mistake
		m_meshletVertexIndices->Deallocate(meshletVerticesView);
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
	auto meshletBoundsView = mesh->GetMeshletBoundsBufferView();
	if (meshletBoundsView != nullptr) {
		m_meshletBoundsBuffer->Deallocate(meshletBoundsView);
	}

	mesh->SetPerMeshBufferView(nullptr);
	mesh->SetBufferViews(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
	mesh->SetCurrentMeshManager(nullptr);
}

void MeshManager::AddMeshInstance(MeshInstance* mesh, bool useMeshletReorderedVertices) {
	mesh->SetCurrentMeshManager(this);
	auto& vertices = useMeshletReorderedVertices ? mesh->GetMesh()->GetMeshletReorderedVertices() : mesh->GetMesh()->GetVertices();
	auto numVertices = mesh->GetMesh()->GetNumVertices(useMeshletReorderedVertices);

	auto vertexSize = mesh->GetMesh()->GetPerMeshCBData().vertexByteSize;
	unsigned int meshInstanceBufferSize = m_perMeshInstanceBuffers->Size();
	if (mesh->HasSkin()) { // Skinned meshes need unique post-skinning vertex buffers
		auto postSkinningView = m_postSkinningVertices->AddData(vertices.data(), numVertices * vertexSize, vertexSize, numVertices * vertexSize * 2); // Allocate twice the size, since we need to ping-pong for motion vectors
		UploadManager::GetInstance().UploadData(vertices.data(), numVertices * vertexSize, postSkinningView->GetBuffer(), postSkinningView->GetOffset() + numVertices * vertexSize);
		auto perMeshInstanceBufferView = m_perMeshInstanceBuffers->AddData(&mesh->GetPerMeshInstanceBufferData(), sizeof(PerMeshInstanceCB), sizeof(PerMeshInstanceCB));
		auto meshletBoundsBufferView = m_meshletBoundsBuffer->AddData(mesh->GetMesh()->GetMeshletBounds().data(), mesh->GetMesh()->GetMeshletCount() * sizeof(BoundingSphere), sizeof(BoundingSphere));
		mesh->SetBufferViews(std::move(postSkinningView), std::move(perMeshInstanceBufferView), std::move(meshletBoundsBufferView));
	}
	else { // Non-skinned meshes can share post-skinning vertex buffers
		auto perMeshInstanceBufferView = m_perMeshInstanceBuffers->AddData(&mesh->GetPerMeshInstanceBufferData(), sizeof(PerMeshInstanceCB), sizeof(PerMeshInstanceCB));
		mesh->SetBufferViewUsingBaseMesh(std::move(perMeshInstanceBufferView));
	}

	if (meshInstanceBufferSize != m_perMeshInstanceBuffers->Size()) {
		m_pCameraManager->SetNumMeshInstances(m_perMeshInstanceBuffers->Size()); // All render views must be updated
	}

	unsigned int meshletBitfieldSize = m_meshletBitfieldBuffer->Size();

	unsigned int bitsToAllocate = mesh->GetMesh()->GetMeshletCount();
	unsigned int bytesToAllocate = (bitsToAllocate + 7) / 8; // Round up to the nearest byte

	auto meshletBitfieldView = m_meshletBitfieldBuffer->Allocate(bytesToAllocate, 1); // 1 bit per meshlet
	if (meshletBitfieldSize != m_meshletBitfieldBuffer->Size()) {
		m_pCameraManager->SetMeshletBitfieldSize(m_meshletBitfieldBuffer->Size()*8); // All render views must be updated
	}
	mesh->SetMeshletBitfieldBufferView(std::move(meshletBitfieldView));
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
	auto meshletBoundsView = mesh->GetMeshletBoundsBufferView();
	if (meshletBoundsView != nullptr) {
		m_meshletBoundsBuffer->Deallocate(meshletBoundsView);
	}
	mesh->SetBufferViews(nullptr, nullptr, nullptr);
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

