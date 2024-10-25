#pragma once

#include <memory>

#include "ResourceHandles.h"
#include "buffers.h"

class RenderableObject;
class BufferView;

class ObjectManager {
public:
	static std::unique_ptr<ObjectManager> CreateUnique() {
		return std::unique_ptr<ObjectManager>(new ObjectManager());
	}
	void AddObject(std::shared_ptr<RenderableObject>& object);
	void RemoveObject(std::shared_ptr<RenderableObject>& object);
	unsigned int GetPerObjectBufferIndex() const {
		return m_perObjectBuffers.index;
	}
	void UpdatePerObjectBuffer(std::unique_ptr<BufferView>&, PerObjectCB& data);
	LazyDynamicStructuredBufferHandle<PerObjectCB>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}
private:
	ObjectManager();
	LazyDynamicStructuredBufferHandle<PerObjectCB> m_perObjectBuffers;
};