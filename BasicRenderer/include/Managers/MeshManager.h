#pragma once

#include <memory>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "buffers.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Materials/MaterialBuckets.h"

class Mesh;
class MeshInstance;
class DynamicBuffer;
class ResourceGroup;
class BufferView;

class MeshManager {
public:
	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	void AddMesh(std::shared_ptr<Mesh>& mesh, MaterialBuckets bucket);
	void AddMeshInstance(MeshInstance* mesh);
	void RemoveMesh(Mesh* mesh);
	void RemoveMeshInstance(MeshInstance* mesh);

	unsigned int GetPreSkinningVertexBufferSRVIndex() const;
	unsigned int GetPostSkinningVertexBufferSRVIndex() const;
	unsigned int GetPostSkinningVertexBufferUAVIndex() const;
	unsigned int GetMeshletOffsetBufferSRVIndex() const;
	unsigned int GetMeshletIndexBufferSRVIndex() const;
	unsigned int GetMeshletTriangleBufferSRVIndex() const;
	std::shared_ptr<ResourceGroup> GetResourceGroup();
	unsigned int GetPerMeshBufferSRVIndex() const;
	std::shared_ptr<DynamicBuffer>& GetPerMeshBuffers();
	std::shared_ptr<DynamicBuffer>& GetPreSkinningVertices();
	std::shared_ptr<DynamicBuffer>& GetPostSkinningVertices();
	unsigned int GetPerMeshInstanceBufferSRVIndex() const;

	void UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data);
	void UpdatePerMeshInstanceBuffer(std::unique_ptr<BufferView>& view, PerMeshInstanceCB& data);
private:
	MeshManager();
	std::shared_ptr<DynamicBuffer> m_preSkinningVertices;
	std::shared_ptr<DynamicBuffer> m_postSkinningVertices;
	std::shared_ptr<DynamicBuffer> m_meshletOffsets;
	std::shared_ptr<DynamicBuffer> m_meshletIndices;
	std::shared_ptr<DynamicBuffer> m_meshletTriangles;

	// Base meshes
	std::shared_ptr<DynamicBuffer> m_perMeshBuffers;

	// Skinned mesh instances
	std::shared_ptr<DynamicBuffer> m_perMeshInstanceBuffers;
	
	std::shared_ptr<ResourceGroup> m_resourceGroup;
};