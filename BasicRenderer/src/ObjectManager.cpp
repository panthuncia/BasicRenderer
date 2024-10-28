#include "ObjectManager.h"

#include <d3d12.h>

#include "ResourceManager.h"
#include "LazyDynamicStructuredBuffer.h"
#include "RenderableObject.h"
ObjectManager::ObjectManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_perObjectBuffers = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerObjectCB>(ResourceState::ALL_SRV, 1, L"perObjectBuffers<PerObjectCB>", 256);
	m_drawSetCommandsBuffer = resourceManager.CreateIndexedDynamicStructuredBuffer<IndirectCommand>(ResourceState::ALL_SRV, 1, L"drawSetCommandsBuffer<IndirectCommand>");

	D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2] = {};
	argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
	argumentDescs[0].ConstantBufferView.RootParameterIndex = 0;
	argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
	commandSignatureDesc.pArgumentDescs = argumentDescs;
	commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
	commandSignatureDesc.ByteStride = sizeof(IndirectCommand);
}
void ObjectManager::AddObject(std::shared_ptr<RenderableObject>& object) {
	object->SetCurrentManager(this);
	std::unique_ptr<BufferView> view = m_perObjectBuffers.buffer->Add();
	m_perObjectBuffers.buffer->UpdateAt(view, object->GetPerObjectCBData());
	object->SetCurrentPerObjectCBView(std::move(view));
	ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(object->GetCurrentPerObjectCBView()->GetBuffer());

	IndirectCommand command = {};
	command.cbv = m_perObjectBuffers.buffer->m_dataBuffer->m_buffer->GetGPUVirtualAddress() + object->GetCurrentPerObjectCBView()->GetOffset();
	unsigned int index = m_drawSetCommandsBuffer.buffer->Add(command);
	object->SetDrawSetIndex(index);
	m_objects.push_back(object);
}

void ObjectManager::RemoveObject(std::shared_ptr<RenderableObject>& object) {
	auto& view = object->GetCurrentPerObjectCBView();
	m_perObjectBuffers.buffer->Remove(view);

	object->SetCurrentPerObjectCBView(nullptr);
	object->SetCurrentManager(nullptr);
	int index = object->GetDrawSetIndex();
	m_drawSetCommandsBuffer.buffer->RemoveAt(index);
	m_objects.erase(m_objects.begin() + index);
	for (int i = index; i < m_objects.size(); i++) {
		m_objects[i]->SetDrawSetIndex(i);
	}
	object->SetDrawSetIndex(-1);
}

void ObjectManager::UpdatePerObjectBuffer(std::unique_ptr<BufferView>& view, PerObjectCB& data) {
	m_perObjectBuffers.buffer->UpdateAt(view, data);
	ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(view->GetBuffer());
}