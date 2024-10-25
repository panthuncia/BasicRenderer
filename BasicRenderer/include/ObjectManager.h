#pragma once

#include <memory>

#include "ResourceHandles.h"
#include "buffers.h"

class RenderableObject;

class ObjectManager {
public:
	static std::unique_ptr<ObjectManager> CreateUnique() {
		return std::unique_ptr<ObjectManager>(new ObjectManager());
	}
	void AddObject(std::shared_ptr<RenderableObject>& object);
	void RemoveObject(std::shared_ptr<RenderableObject>& object);
private:
	ObjectManager();
	LazyDynamicStructuredBufferHandle<PerObjectCB> m_perObjectBuffers;
};