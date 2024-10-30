#include "ObjectManager.h"

#include <d3d12.h>

#include "ResourceManager.h"
#include "LazyDynamicStructuredBuffer.h"
#include "RenderableObject.h"
#include "DynamicBuffer.h"
#include "SortedUnsignedIntBuffer.h"

ObjectManager::ObjectManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_perObjectBuffers = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerObjectCB>(ResourceState::ALL_SRV, 1, L"perObjectBuffers<PerObjectCB>", 256);
	m_drawSetCommandsBuffer = resourceManager.CreateIndexedDynamicStructuredBuffer<IndirectCommand>(ResourceState::ALL_SRV, 1, L"drawSetCommandsBuffer<IndirectCommand>");
	m_activeDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeDrawSetIndices");

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

	std::vector<unsigned int> indices;
	// For each mesh, add an indirect command to the draw set buffer
	for (int i = 0; i < object->GetOpaqueMeshes().size(); i++) {
		IndirectCommand command = {};
		command.perObjectCBV = m_perObjectBuffers.buffer->m_dataBuffer->m_buffer->GetGPUVirtualAddress() + object->GetCurrentPerObjectCBView()->GetOffset();
		unsigned int index = m_drawSetCommandsBuffer->Add(command);
		indices.push_back(index);
		m_activeDrawSetIndices->Insert(index);
	}
	object->SetCurrentDrawSetIndices(indices);
	//m_objects.push_back(object);

	auto& manager = ResourceManager::GetInstance();
	manager.QueueDynamicBufferUpdate(m_drawSetCommandsBuffer.get());
	manager.QueueDynamicBufferUpdate(m_activeDrawSetIndices.get());
	manager.QueueViewedDynamicBufferViewUpdate(object->GetCurrentPerObjectCBView()->GetBuffer());

}

void ObjectManager::RemoveObject(std::shared_ptr<RenderableObject>& object) {
	auto& view = object->GetCurrentPerObjectCBView();
	m_perObjectBuffers.buffer->Remove(view);

	object->SetCurrentPerObjectCBView(nullptr);
	object->SetCurrentManager(nullptr);
	auto& indices = object->GetCurrentDrawSetIndices();
	for (auto index : indices) {
		m_drawSetCommandsBuffer->RemoveAt(index);
		m_activeDrawSetIndices->Remove(index);
	}
	object->SetCurrentDrawSetIndices({});
	//m_objects.erase(m_objects.begin() + index);
}

void ObjectManager::UpdatePerObjectBuffer(std::unique_ptr<BufferView>& view, PerObjectCB& data) {
	m_perObjectBuffers.buffer->UpdateAt(view, data);
	ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(view->GetBuffer());
}