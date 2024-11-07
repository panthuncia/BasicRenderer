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
	m_opaqueDrawSetCommandsBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<IndirectCommand>(ResourceState::ALL_SRV, 1, L"drawSetCommandsBuffer<IndirectCommand>", 1);
	m_transparentDrawSetCommandsBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<IndirectCommand>(ResourceState::ALL_SRV, 1, L"drawSetCommandsBuffer<IndirectCommand>", 1);
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
		std::vector<std::shared_ptr<BufferView>> views;
		// For each mesh, add an indirect command to the draw set buffer
		for (auto& mesh : object->GetOpaqueMeshes()) {
			IndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			auto view = m_opaqueDrawSetCommandsBuffer->Add();
			views.push_back(view);
			m_opaqueDrawSetCommandsBuffer->UpdateAt(view.get(), command);
			unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
			indices.push_back(index);
			m_activeOpaqueDrawSetIndices->Insert(index);
		}

		object->SetCurrentOpaqueDrawSetCommandViews(views);
		object->SetCurrentOpaqueDrawSetIndices(indices);

		// TODO: Instead of inserting one update for every object, insert one update for all objects
		m_activeOpaqueDrawSetIndices->UpdateUploadBuffer();
		manager.QueueDynamicBufferUpdate(m_opaqueDrawSetCommandsBuffer.get());
		manager.QueueDynamicBufferUpdate(m_activeOpaqueDrawSetIndices.get());
	}

	if (object->HasTransparent()) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		for (auto& mesh : object->GetTransparentMeshes()) {
			IndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			auto view = m_transparentDrawSetCommandsBuffer->Add();
			views.push_back(view);
			m_transparentDrawSetCommandsBuffer->UpdateAt(view.get(), command);
			unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
			indices.push_back(index);
			m_activeTransparentDrawSetIndices->Insert(index);
		}
		object->SetCurrentTransparentDrawSetIndices(indices);
		object->SetCurrentTransparentDrawSetCommandViews(views);
		m_activeTransparentDrawSetIndices->UpdateUploadBuffer();
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
	auto& opaqueViews = object->GetCurrentOpaqueDrawSetCommandViews();
	for (auto view : opaqueViews) {
		m_opaqueDrawSetCommandsBuffer->Remove(view.get());
		unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
		m_activeOpaqueDrawSetIndices->Remove(index);
	}
	object->SetCurrentOpaqueDrawSetIndices({});
	object->SetCurrentOpaqueDrawSetCommandViews({});

	auto& transparentViews = object->GetCurrentTransparentDrawSetCommandViews();
	for (auto view : transparentViews) {
		m_transparentDrawSetCommandsBuffer->Remove(view.get());
		unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
		m_activeTransparentDrawSetIndices->Remove(index);
	}
	object->SetCurrentTransparentDrawSetIndices({});
	object->SetCurrentTransparentDrawSetCommandViews({});

	m_activeOpaqueDrawSetIndices->UpdateUploadBuffer();
	m_activeTransparentDrawSetIndices->UpdateUploadBuffer();
	auto& manager = ResourceManager::GetInstance();
	manager.QueueDynamicBufferUpdate(m_activeOpaqueDrawSetIndices.get());
	manager.QueueDynamicBufferUpdate(m_activeTransparentDrawSetIndices.get());


	//m_objects.erase(m_objects.begin() + index);
}

void ObjectManager::UpdatePerObjectBuffer(BufferView* view, PerObjectCB& data) {
	m_perObjectBuffers->UpdateAt(view, data);
	ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(view->GetBuffer());
}