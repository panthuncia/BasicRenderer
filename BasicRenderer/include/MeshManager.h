#pragma once

#include <memory>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "DynamicBuffer.h"
#include "ResourceHandles.h"
#include "BufferView.h"
#include "ResourceGroup.h"

class Mesh;
class DynamicBuffer;

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
private:
	MeshManager();
	std::shared_ptr<DynamicBuffer> m_vertices;
	std::shared_ptr<DynamicBuffer> m_meshletOffsets;
	std::shared_ptr<DynamicBuffer> m_meshletIndices;
	std::shared_ptr<DynamicBuffer> m_meshletTriangles;

	std::shared_ptr<ResourceGroup> m_resourceGroup;
};