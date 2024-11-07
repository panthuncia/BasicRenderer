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
	std::shared_ptr<LazyDynamicStructuredBuffer<PerObjectCB>>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}

	unsigned int GetOpaqueDrawSetCommandsBufferSRVIndex() const {
		return m_opaqueDrawSetCommandsBuffer->GetSRVInfo().index;
	}
	
	unsigned int GetTransparentDrawSetCommandsBufferSRVIndex() const {
		return m_transparentDrawSetCommandsBuffer->GetSRVInfo().index;
	}

	unsigned int GetActiveOpaqueDrawSetIndicesBufferSRVIndex() const {
		return m_activeOpaqueDrawSetIndices->GetSRVInfo().index;
	}

	unsigned int GetActiveTransparentDrawSetIndicesBufferSRVIndex() const {
		return m_activeTransparentDrawSetIndices->GetSRVInfo().index;
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<IndirectCommand>>& GetOpaqueDrawSetCommandsBuffer() {
		return m_opaqueDrawSetCommandsBuffer;
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<IndirectCommand>>& GetTransparentDrawSetCommandsBuffer() {
		return m_transparentDrawSetCommandsBuffer;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveOpaqueDrawSetIndices() {
		return m_activeOpaqueDrawSetIndices;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveTransparentDrawSetIndices() {
		return m_activeTransparentDrawSetIndices;
	}

private:
	ObjectManager();
	std::vector<std::shared_ptr<RenderableObject>> m_objects;
	std::shared_ptr<LazyDynamicStructuredBuffer<PerObjectCB>> m_perObjectBuffers;
	std::shared_ptr<LazyDynamicStructuredBuffer<IndirectCommand>> m_opaqueDrawSetCommandsBuffer;
	std::shared_ptr<LazyDynamicStructuredBuffer<IndirectCommand>> m_transparentDrawSetCommandsBuffer;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeOpaqueDrawSetIndices;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeTransparentDrawSetIndices;
};