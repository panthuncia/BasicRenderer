#include "ObjectManager.h"

#include "ResourceManager.h"
#include "LazyDynamicStructuredBuffer.h"
#include "RenderableObject.h"
ObjectManager::ObjectManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_perObjectBuffers = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerObjectCB>(ResourceState::ALL_SRV, 1, L"perObjectBuffers<PerObjectCB>", 256);
}
void ObjectManager::AddObject(std::shared_ptr<RenderableObject>& object) {
	object->SetCurrentManager(this);
	std::unique_ptr<BufferView> view = m_perObjectBuffers.buffer->Add();
	m_perObjectBuffers.buffer->UpdateAt(view, object->GetPerObjectCBData());
	object->SetCurrentPerObjectCBView(std::move(view));
	ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(object->GetCurrentPerObjectCBView()->GetBuffer());
}

void ObjectManager::RemoveObject(std::shared_ptr<RenderableObject>& object) {
	auto& view = object->GetCurrentPerObjectCBView();
	m_perObjectBuffers.buffer->Remove(view);

	object->SetCurrentPerObjectCBView(nullptr);
}

void ObjectManager::UpdatePerObjectBuffer(std::unique_ptr<BufferView>& view, PerObjectCB& data) {
	m_perObjectBuffers.buffer->UpdateAt(view, data);
	ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(view->GetBuffer());
}