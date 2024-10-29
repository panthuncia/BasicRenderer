#pragma once

#include <memory>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "DynamicBuffer.h"
#include "ResourceHandles.h"
#include "BufferView.h"
#include "ResourceGroup.h"

class Mesh;

class MeshManager {
public:
	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	void AddMesh(std::shared_ptr<Mesh>& mesh);
	void RemoveMesh(std::shared_ptr<BufferView> view);
	unsigned int GetVertexBufferIndex() const {
		return m_vertices.buffer->GetSRVInfo().index;
	}
	unsigned int GetMeshletOffsetBufferIndex() const {
		return m_meshletOffsets.buffer->GetSRVInfo().index;
	}
	unsigned int GetMeshletIndexBufferIndex() const {
		return m_meshletIndices.buffer->GetSRVInfo().index;
	}
	unsigned int GetMeshletTriangleBufferIndex() const {
		return m_meshletTriangles.buffer->GetSRVInfo().index;
	}
	std::shared_ptr<ResourceGroup> GetResourceGroup() {
		return m_resourceGroup;
	}
private:
	MeshManager();
	DynamicBufferHandle m_vertices;
	DynamicBufferHandle m_meshletOffsets;
	DynamicBufferHandle m_meshletIndices;
	DynamicBufferHandle m_meshletTriangles;
	std::shared_ptr<ResourceGroup> m_resourceGroup;
};