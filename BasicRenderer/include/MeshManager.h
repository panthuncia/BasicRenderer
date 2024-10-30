#pragma once

#include <memory>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "buffers.h"
#include "LazyDynamicStructuredBuffer.h"

class Mesh;
class DynamicBuffer;
class ResourceGroup;
class BufferView;

class MeshManager {
public:
	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	void AddMesh(std::shared_ptr<Mesh>& mesh);
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
	unsigned int GetPerMeshBufferSRVIndex() const {
		return m_perMeshBuffers->GetSRVInfo().index;
	}
	std::shared_ptr<LazyDynamicStructuredBuffer<PerMeshCB>>& GetPerMeshBuffers() {
		return m_perMeshBuffers;
	}
	void UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data);
private:
	MeshManager();
	std::shared_ptr<DynamicBuffer> m_vertices;
	std::shared_ptr<DynamicBuffer> m_meshletOffsets;
	std::shared_ptr<DynamicBuffer> m_meshletIndices;
	std::shared_ptr<DynamicBuffer> m_meshletTriangles;

	std::shared_ptr<LazyDynamicStructuredBuffer<PerMeshCB>> m_perMeshBuffers;

	std::shared_ptr<ResourceGroup> m_resourceGroup;
};