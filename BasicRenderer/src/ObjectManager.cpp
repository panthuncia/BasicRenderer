#include "ObjectManager.h"

#include <d3d12.h>

#include "ResourceManager.h"
#include "LazyDynamicStructuredBuffer.h"
#include "RenderableObject.h"
#include "DynamicBuffer.h"
#include "SortedUnsignedIntBuffer.h"

ObjectManager::ObjectManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_perObjectBuffers = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerObjectCB>(ResourceState::ALL_SRV, 1, L"perObjectBuffers<PerObjectCB>", 1);
	m_opaqueDrawSetCommandsBuffer = resourceManager.CreateIndexedDynamicStructuredBuffer<IndirectCommand>(ResourceState::ALL_SRV, 1, L"drawSetCommandsBuffer<IndirectCommand>");
	m_transparentDrawSetCommandsBuffer = resourceManager.CreateIndexedDynamicStructuredBuffer<IndirectCommand>(ResourceState::ALL_SRV, 1, L"drawSetCommandsBuffer<IndirectCommand>");
	m_activeOpaqueDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeOpaqueDrawSetIndices");
	m_activeTransparentDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeTransparentDrawSetIndices");
}
void ObjectManager::AddObject(std::shared_ptr<RenderableObject>& object) {
	object->SetCurrentManager(this);
	std::shared_ptr<BufferView> view = m_perObjectBuffers->Add();
	m_perObjectBuffers->UpdateAt(view.get(), object->GetPerObjectCBData());
	object->SetCurrentPerObjectCBView(view);

	auto& manager = ResourceManager::GetInstance();
	manager.QueueViewedDynamicBufferViewUpdate(object->GetCurrentPerObjectCBView()->GetBuffer());


	if (object->HasOpaque()) {
		std::vector<unsigned int> indices;
		// For each mesh, add an indirect command to the draw set buffer
		for (auto& mesh : object->GetOpaqueMeshes()) {
			IndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			unsigned int index = m_opaqueDrawSetCommandsBuffer->Add(command);
			indices.push_back(index);
			m_activeOpaqueDrawSetIndices->Insert(index);
		}

		object->SetCurrentOpaqueDrawSetIndices(indices);

		// TODO: Instead of inserting one update for every object, insert one update for all objects
		manager.QueueDynamicBufferUpdate(m_opaqueDrawSetCommandsBuffer.get());
		manager.QueueDynamicBufferUpdate(m_activeOpaqueDrawSetIndices.get());
	}

	if (object->HasTransparent()) {
		std::vector<unsigned int> indices;
		for (auto& mesh : object->GetTransparentMeshes()) {
			IndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			unsigned int index = m_transparentDrawSetCommandsBuffer->Add(command);
			indices.push_back(index);
			m_activeTransparentDrawSetIndices->Insert(index);
		}
		object->SetCurrentTransparentDrawSetIndices(indices);
		manager.QueueDynamicBufferUpdate(m_transparentDrawSetCommandsBuffer.get());
		manager.QueueDynamicBufferUpdate(m_activeTransparentDrawSetIndices.get());
	}

	//m_objects.push_back(object);
}

void ObjectManager::RemoveObject(std::shared_ptr<RenderableObject>& object) {
	auto& view = object->GetCurrentPerObjectCBView();
	m_perObjectBuffers->Remove(view.get());

	DeletionManager::GetInstance().MarkForDelete(view);

	object->SetCurrentManager(nullptr);

	// Remove the object's draw set commands from the draw set buffers
	auto& indices = object->GetCurrentOpaqueDrawSetIndices();
	for (auto index : indices) {
		m_opaqueDrawSetCommandsBuffer->RemoveAt(index);
		m_activeOpaqueDrawSetIndices->Remove(index);
	}
	object->SetCurrentOpaqueDrawSetIndices({});

	indices = object->GetCurrentTransparentDrawSetIndices();
	for (auto index : indices) {
		m_transparentDrawSetCommandsBuffer->RemoveAt(index);
		m_activeTransparentDrawSetIndices->Remove(index);
	}
	object->SetCurrentTransparentDrawSetIndices({});

	//m_objects.erase(m_objects.begin() + index);
}

void ObjectManager::UpdatePerObjectBuffer(BufferView* view, PerObjectCB& data) {
	m_perObjectBuffers->UpdateAt(view, data);
	ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(view->GetBuffer());
}