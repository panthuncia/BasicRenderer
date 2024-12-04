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
	unsigned int GetPerObjectBufferSRVIndex(uint8_t frameIndex) const {
		return m_perObjectBuffers->GetSRVInfo(frameIndex).index;
	}
	void UpdatePerObjectBuffer(BufferView*, PerObjectCB& data);
	std::shared_ptr<LazyDynamicStructuredBuffer<PerObjectCB>>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}

	unsigned int GetOpaqueDrawSetCommandsBufferSRVIndex(uint8_t frameIndex) const {
		return m_opaqueDrawSetCommandsBuffer->GetSRVInfo(frameIndex).index;
	}
	
	unsigned int GetTransparentDrawSetCommandsBufferSRVIndex(uint8_t frameIndex) const {
		return m_transparentDrawSetCommandsBuffer->GetSRVInfo(frameIndex).index;
	}

	unsigned int GetActiveOpaqueDrawSetIndicesBufferSRVIndex(uint8_t frameIndex) const {
		return m_activeOpaqueDrawSetIndices->GetSRVInfo(frameIndex).index;
	}

	unsigned int GetActiveTransparentDrawSetIndicesBufferSRVIndex(uint8_t frameIndex) const {
		return m_activeTransparentDrawSetIndices->GetSRVInfo(frameIndex).index;
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