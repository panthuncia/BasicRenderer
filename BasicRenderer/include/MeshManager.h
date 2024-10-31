#pragma once

#include <memory>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "buffers.h"
#include "LazyDynamicStructuredBuffer.h"
#include "MaterialBuckets.h"

class Mesh;
class DynamicBuffer;
class ResourceGroup;
class BufferView;

class MeshManager {
public:
	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	void AddMesh(std::shared_ptr<Mesh>& mesh, MaterialBuckets bucket);
	void RemoveMesh(std::shared_ptr<BufferView> view);
	unsigned int GetVertexBufferIndex() const {
		return m_vertices->GetSRVInfo().index;
	}
	unsigned int GetMeshletOffsetBufferIndex() const {
		return m_meshletOffsets->GetSRVInfo().index;
	}
	unsigned int GetMeshletIndexBufferIndex() const {
		return m_meshletIndices->GetSRVInfo().index;
	}
	unsigned int GetMeshletTriangleBufferIndex() const {
		return m_meshletTriangles->GetSRVInfo().index;
	}
	std::shared_ptr<ResourceGroup> GetResourceGroup() {
		return m_resourceGroup;
	}
	unsigned int GetOpaquePerMeshBufferSRVIndex() const {
		return m_opaquePerMeshBuffers->GetSRVInfo().index;
	}
	std::shared_ptr<DynamicBuffer>& GetOpaquePerMeshBuffers() {
		return m_opaquePerMeshBuffers;
	}

	unsigned int GetTransparentPerMeshBufferSRVIndex() const {
		return m_transparentPerMeshBuffers->GetSRVInfo().index;
	}
	std::shared_ptr<DynamicBuffer>& GetTransparentPerMeshBuffers() {
		return m_transparentPerMeshBuffers;
	}

	void UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data);
private:
	MeshManager();
	std::shared_ptr<DynamicBuffer> m_vertices;
	std::shared_ptr<DynamicBuffer> m_meshletOffsets;
	std::shared_ptr<DynamicBuffer> m_meshletIndices;
	std::shared_ptr<DynamicBuffer> m_meshletTriangles;

	std::shared_ptr<DynamicBuffer> m_opaquePerMeshBuffers;
	std::shared_ptr<DynamicBuffer> m_transparentPerMeshBuffers;

	std::shared_ptr<ResourceGroup> m_resourceGroup;
};