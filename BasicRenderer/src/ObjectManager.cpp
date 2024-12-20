#include "ObjectManager.h"

#include <d3d12.h>

#include "ResourceManager.h"
#include "LazyDynamicStructuredBuffer.h"
#include "RenderableObject.h"
#include "DynamicBuffer.h"
#include "SortedUnsignedIntBuffer.h"

ObjectManager::ObjectManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	//m_perObjectBuffers = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<PerObjectCB>(ResourceState::ALL_SRV, 1, L"perObjectBuffers<PerObjectCB>", 1);
	m_perObjectBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerObjectCB), 1, ResourceState::ALL_SRV, L"perObjectBuffers<PerObjectCB>");
	//m_opaqueDrawSetCommandsBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<IndirectCommand>(ResourceState::ALL_SRV, 1, L"drawSetCommandsBuffer<IndirectCommand>", 1);
	m_opaqueDrawSetCommandsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(IndirectCommand), 1, ResourceState::ALL_SRV, L"opaqueDrawSetCommandsBuffer<IndirectCommand>");
	//m_transparentDrawSetCommandsBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<IndirectCommand>(ResourceState::ALL_SRV, 1, L"drawSetCommandsBuffer<IndirectCommand>", 1);
	m_alphaTestDrawSetCommandsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(IndirectCommand), 1, ResourceState::ALL_SRV, L"alphaTestDrawSetCommandsBuffer<IndirectCommand>");
	m_blendDrawSetCommandsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(IndirectCommand), 1, ResourceState::ALL_SRV, L"blendDrawSetCommandsBuffer<IndirectCommand>");
	
	m_activeOpaqueDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeOpaqueDrawSetIndices");
	m_activeAlphaTestDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeTransparentDrawSetIndices");
	m_activeBlendDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeBlendDrawSetIndices");
}
void ObjectManager::AddObject(std::shared_ptr<RenderableObject>& object) {
	object->SetCurrentManager(this);
	std::shared_ptr<BufferView> view = m_perObjectBuffers->AddData(&object->GetPerObjectCBData(), sizeof(PerObjectCB), typeid(PerObjectCB));
	// m_perObjectBuffers->UpdateView(view.get(), &object->GetPerObjectCBData());
	object->SetCurrentPerObjectCBView(view);

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
			std::shared_ptr<BufferView> view = m_opaqueDrawSetCommandsBuffer->AddData(&command, sizeof(IndirectCommand), typeid(IndirectCommand));
			views.push_back(view);
			//m_opaqueDrawSetCommandsBuffer->UpdateView(view.get(), &command);
			unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
			indices.push_back(index);
			m_activeOpaqueDrawSetIndices->Insert(index);
		}

		object->SetCurrentOpaqueDrawSetCommandViews(views);
		object->SetCurrentOpaqueDrawSetIndices(indices);
	}

	if (object->HasAlphaTest()) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		for (auto& mesh : object->GetAlphaTestMeshes()) {
			IndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_alphaTestDrawSetCommandsBuffer->AddData(&command, sizeof(IndirectCommand), typeid(IndirectCommand));
			views.push_back(view);
			//m_transparentDrawSetCommandsBuffer->UpdateView(view.get(), &command);
			unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
			indices.push_back(index);
			m_activeAlphaTestDrawSetIndices->Insert(index);
		}
		object->SetCurrentAlphaTestDrawSetIndices(indices);
		object->SetCurrentAlphaTestDrawSetCommandViews(views);
	}

	if (object->HasBlend()) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		for (auto& mesh : object->GetBlendMeshes()) {
			IndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_blendDrawSetCommandsBuffer->AddData(&command, sizeof(IndirectCommand), typeid(IndirectCommand));
			views.push_back(view);

			unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
			indices.push_back(index);
			m_activeBlendDrawSetIndices->Insert(index);
		}
		object->SetCurrentBlendDrawSetIndices(indices);
		object->SetCurrentBlendDrawSetCommandViews(views);
	}

	//m_objects.push_back(object);
}

void ObjectManager::RemoveObject(std::shared_ptr<RenderableObject>& object) {
	auto& view = object->GetCurrentPerObjectCBView();
	m_perObjectBuffers->Deallocate(view);

	DeletionManager::GetInstance().MarkForDelete(view);

	object->SetCurrentManager(nullptr);

	// Remove the object's draw set commands from the draw set buffers
	auto& opaqueViews = object->GetCurrentOpaqueDrawSetCommandViews();
	for (auto view : opaqueViews) {
		m_opaqueDrawSetCommandsBuffer->Deallocate(view);
		unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
		m_activeOpaqueDrawSetIndices->Remove(index);
	}
	object->SetCurrentOpaqueDrawSetIndices({});
	object->SetCurrentOpaqueDrawSetCommandViews({});

	auto& transparentViews = object->GetCurrentAlphaTestDrawSetCommandViews();
	for (auto view : transparentViews) {
		m_alphaTestDrawSetCommandsBuffer->Deallocate(view);
		unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
		m_activeAlphaTestDrawSetIndices->Remove(index);
	}
	object->SetCurrentAlphaTestDrawSetIndices({});
	object->SetCurrentAlphaTestDrawSetCommandViews({});

	auto& blendViews = object->GetCurrentBlendDrawSetCommandViews();
	for (auto view : blendViews) {
		m_blendDrawSetCommandsBuffer->Deallocate(view);
		unsigned int index = view->GetOffset() / sizeof(IndirectCommand);
		m_activeBlendDrawSetIndices->Remove(index);
	}
	object->SetCurrentBlendDrawSetIndices({});
}

void ObjectManager::UpdatePerObjectBuffer(BufferView* view, PerObjectCB& data) {
	m_perObjectBuffers->UpdateView(view, &data);
}