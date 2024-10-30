#pragma once

#include <memory>

#include "LazyDynamicStructuredBuffer.h"
#include "DynamicStructuredBuffer.h"
#include "buffers.h"


class RenderableObject;
class BufferView;
class DynamicBuffer;
class SortedUnsignedIntBuffer;

struct IndirectCommand {
	D3D12_GPU_VIRTUAL_ADDRESS perMeshCBV;
	D3D12_GPU_VIRTUAL_ADDRESS perObjectCBV;
};

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
	void UpdatePerObjectBuffer(std::unique_ptr<BufferView>&, PerObjectCB& data);
	std::shared_ptr<LazyDynamicStructuredBuffer<PerObjectCB>>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}
private:
	ObjectManager();
	std::vector<std::shared_ptr<RenderableObject>> m_objects;
	std::shared_ptr<LazyDynamicStructuredBuffer<PerObjectCB>> m_perObjectBuffers;
	std::shared_ptr<DynamicStructuredBuffer<IndirectCommand>> m_opaqueDrawSetCommandsBuffer;
	std::shared_ptr<DynamicStructuredBuffer<IndirectCommand>> m_transparentDrawSetCommandsBuffer;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeOpaqueDrawSetIndices;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeTransparentDrawSetIndices;
};