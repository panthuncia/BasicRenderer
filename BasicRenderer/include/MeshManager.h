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
	void RemoveMesh(Mesh* mesh);

	unsigned int GetPreSkinningVertexBufferSRVIndex() const {
		return m_preSkinningVertices->GetSRVInfo().index;
	}
	unsigned int GetPostSkinningVertexBufferSRVIndex() const {
		return m_postSkinningVertices->GetSRVInfo().index;
	}
	unsigned int GetPostSkinningVertexBufferUAVIndex() const {
		return m_postSkinningVertices->GetUAVShaderVisibleInfo().index;
	}
	unsigned int GetMeshletOffsetBufferSRVIndex() const {
		return m_meshletOffsets->GetSRVInfo().index;
	}
	unsigned int GetMeshletIndexBufferSRVIndex() const {
		return m_meshletIndices->GetSRVInfo().index;
	}
	unsigned int GetMeshletTriangleBufferSRVIndex() const {
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
	unsigned int GetAlphaTestPerMeshBufferSRVIndex() const {
		return m_alphaTestPerMeshBuffers->GetSRVInfo().index;
	}
	std::shared_ptr<DynamicBuffer>& GetAlphaTestPerMeshBuffers() {
		return m_alphaTestPerMeshBuffers;
	}
	unsigned int GetBlendPerMeshBufferSRVIndex() const {
		return m_blendPerMeshBuffers->GetSRVInfo().index;
	}
	std::shared_ptr<DynamicBuffer>& GetBlendPerMeshBuffers() {
		return m_blendPerMeshBuffers;
	}
	std::shared_ptr<DynamicBuffer>& GetPreSkinningVertices() {
		return m_preSkinningVertices;
	}
	std::shared_ptr<DynamicBuffer>& GetPostSkinningVertices() {
		return m_postSkinningVertices;
	}

	void UpdatePerMeshBuffer(std::unique_ptr<BufferView>& view, PerMeshCB& data);
private:
	MeshManager();
	std::shared_ptr<DynamicBuffer> m_preSkinningVertices;
	std::shared_ptr<DynamicBuffer> m_postSkinningVertices;
	std::shared_ptr<DynamicBuffer> m_meshletOffsets;
	std::shared_ptr<DynamicBuffer> m_meshletIndices;
	std::shared_ptr<DynamicBuffer> m_meshletTriangles;

	std::shared_ptr<DynamicBuffer> m_opaquePerMeshBuffers;
	std::shared_ptr<DynamicBuffer> m_alphaTestPerMeshBuffers;
	std::shared_ptr<DynamicBuffer> m_blendPerMeshBuffers;

	std::shared_ptr<ResourceGroup> m_resourceGroup;
};