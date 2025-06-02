#pragma once

#include <memory>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "ShaderBuffers.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Materials/MaterialBuckets.h"
#include "Interfaces/IResourceProvider.h"

class Mesh;
class MeshInstance;
class DynamicBuffer;
class ResourceGroup;
class BufferView;
class CameraManager;

class MeshManager : public IResourceProvider {
public:
	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	void AddMesh(std::shared_ptr<Mesh>& mesh, MaterialBuckets bucket, bool useMeshletReorderedVertices);
	void AddMeshInstance(MeshInstance* mesh, bool useMeshletReorderedVertices);
	void RemoveMesh(Mesh* mesh);
	void RemoveMeshInstance(MeshInstance* mesh);

	unsigned int GetPreSkinningVertexBufferSRVIndex() const;
	unsigned int GetPostSkinningVertexBufferSRVIndex() const;
	unsigned int GetPostSkinningVertexBufferUAVIndex() const;
	unsigned int GetMeshletOffsetBufferSRVIndex() const;
	unsigned int GetMeshletVertexIndexBufferSRVIndex() const;
	unsigned int GetMeshletTriangleBufferSRVIndex() const;
	unsigned int GetMeshletBoundsBufferSRVIndex() const;
	std::shared_ptr<ResourceGroup> GetResourceGroup();
	unsigned int GetPerMeshBufferSRVIndex() const;
	std::shared_ptr<DynamicBuffer>& GetPerMeshBuffers();
	std::shared_ptr<DynamicBuffer>& GetPreSkinningVertices();
	std::shared_ptr<DynamicBuffer>& GetPostSkinningVertices();
	unsigned int GetPerMeshInstanceBufferSRVIndex() const;

	void UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data);
	void UpdatePerMeshInstanceBuffer(std::unique_ptr<BufferView>& view, PerMeshInstanceCB& data);
	void SetCameraManager(CameraManager* cameraManager) { m_pCameraManager = cameraManager; }

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
	MeshManager();
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
	
	std::shared_ptr<ResourceGroup> m_resourceGroup;

	CameraManager* m_pCameraManager;
};