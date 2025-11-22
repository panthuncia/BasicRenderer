#pragma once

#include <memory>

#include "ShaderBuffers.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Materials/MaterialBuckets.h"
#include "Interfaces/IResourceProvider.h"

class Mesh;
class MeshInstance;
class DynamicBuffer;
class ResourceGroup;
class BufferView;
class ViewManager;

// TODO: Find better way of batching these with namespaces
#define MESH_RESOURCE_IDFENTIFIERS Builtin::MeshResources::MeshletBounds, Builtin::MeshResources::MeshletOffsets, Builtin::MeshResources::MeshletVertexIndices, Builtin::MeshResources::MeshletTriangles

class MeshManager : public IResourceProvider {
public:
	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	void AddMesh(std::shared_ptr<Mesh>& mesh, bool useMeshletReorderedVertices);
	void AddMeshInstance(MeshInstance* mesh, bool useMeshletReorderedVertices);
	void RemoveMesh(Mesh* mesh);
	void RemoveMeshInstance(MeshInstance* mesh);

	void UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data);
	void UpdatePerMeshInstanceBuffer(std::unique_ptr<BufferView>& view, PerMeshInstanceCB& data);
	void SetViewManager(ViewManager* viewManager) { m_pViewManager = viewManager; }

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
	MeshManager();
	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::shared_ptr<DynamicBuffer> m_preSkinningVertices;
	std::shared_ptr<DynamicBuffer> m_postSkinningVertices;
	std::shared_ptr<DynamicBuffer> m_meshletOffsets;
	std::shared_ptr<DynamicBuffer> m_meshletVertexIndices;
	std::shared_ptr<DynamicBuffer> m_meshletTriangles;
	std::shared_ptr<DynamicBuffer> m_meshletBoundsBuffer;
	std::shared_ptr<DynamicBuffer> m_meshletBitfieldBuffer;

	// Base meshes
	std::shared_ptr<DynamicBuffer> m_perMeshBuffers;

	// Skinned mesh instances
	std::shared_ptr<DynamicBuffer> m_perMeshInstanceBuffers;

	ViewManager* m_pViewManager;
};