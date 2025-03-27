#pragma once

#include <memory>

#include "LazyDynamicStructuredBuffer.h"
#include "DynamicStructuredBuffer.h"
#include "buffers.h"
#include "SortedUnsignedIntBuffer.h"
#include "IndirectCommand.h"

class RenderableObject;
class BufferView;
class DynamicBuffer;

class ObjectManager {
public:
	static std::unique_ptr<ObjectManager> CreateUnique() {
		return std::unique_ptr<ObjectManager>(new ObjectManager());
	}
	void AddObject(std::shared_ptr<RenderableObject>& object);
	void RemoveObject(std::shared_ptr<RenderableObject>& object);
	unsigned int GetPerObjectBufferSRVIndex() const {
		return m_perObjectBuffers->GetSRVInfo().index;
	}
	void UpdatePerObjectBuffer(BufferView*, PerObjectCB& data);
	void UpdateNormalMatrixBuffer(BufferView* view, void* data);
	std::shared_ptr<DynamicBuffer>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}

	unsigned int GetMasterIndirectCommandsBufferSRVIndex() const {
		return m_masterIndirectCommandsBuffer->GetSRVInfo().index;
	}

	unsigned int GetActiveOpaqueDrawSetIndicesBufferSRVIndex() const {
		return m_activeOpaqueDrawSetIndices->GetSRVInfo().index;
	}

	unsigned int GetActiveAlphaTestDrawSetIndicesBufferSRVIndex() const {
		return m_activeAlphaTestDrawSetIndices->GetSRVInfo().index;
	}

	unsigned int GetActiveBlendDrawSetIndicesBufferSRVIndex() const {
		return m_activeBlendDrawSetIndices->GetSRVInfo().index;
	}

	unsigned int GetNormalMatrixBufferSRVIndex() const {
		return m_normalMatrixBuffer->GetSRVInfo().index;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveOpaqueDrawSetIndices() {
		return m_activeOpaqueDrawSetIndices;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveAlphaTestDrawSetIndices() {
		return m_activeAlphaTestDrawSetIndices;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveBlendDrawSetIndices() {
		return m_activeBlendDrawSetIndices;
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>>& GetNormalMatrixBuffer() {
		return m_normalMatrixBuffer;
	}


private:
	ObjectManager();
	std::vector<std::shared_ptr<RenderableObject>> m_objects;
	std::shared_ptr<DynamicBuffer> m_perObjectBuffers; // Per object constant buffer
	std::shared_ptr<DynamicBuffer> m_masterIndirectCommandsBuffer; // Indirect draw command buffer
	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>> m_normalMatrixBuffer; // Normal matrices for each object
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeOpaqueDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active opaque objects
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeAlphaTestDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active alpha tested objects
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeBlendDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active blended objects
	std::shared_ptr<LazyDynamicStructuredBuffer<PerMeshInstanceCB>> m_perMeshInstanceBuffers; // Indices into m_perObjectBuffers for each mesh instance in each object
};