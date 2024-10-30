#pragma once

#include <memory>

#include "ResourceHandles.h"
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
	unsigned int GetPerObjectBufferIndex() const {
		return m_perObjectBuffers.buffer->GetSRVInfo().index;
	}
	void UpdatePerObjectBuffer(std::unique_ptr<BufferView>&, PerObjectCB& data);
	LazyDynamicStructuredBufferHandle<PerObjectCB>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}
private:
	ObjectManager();
	std::vector<std::shared_ptr<RenderableObject>> m_objects;
	LazyDynamicStructuredBufferHandle<PerObjectCB> m_perObjectBuffers;
	std::shared_ptr<DynamicStructuredBuffer<IndirectCommand>> m_drawSetCommandsBuffer;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeDrawSetIndices;
};