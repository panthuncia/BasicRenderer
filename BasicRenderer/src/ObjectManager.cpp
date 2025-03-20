#include "ObjectManager.h"

#include <d3d12.h>

#include "ResourceManager.h"
#include "LazyDynamicStructuredBuffer.h"
#include "RenderableObject.h"
#include "DynamicBuffer.h"
#include "SortedUnsignedIntBuffer.h"

ObjectManager::ObjectManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_perObjectBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerObjectCB), 1, ResourceState::ALL_SRV, L"perObjectBuffers<PerObjectCB>");
	m_opaqueDrawSetCommandsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(DispatchMeshIndirectCommand), 1, ResourceState::ALL_SRV, L"opaqueDrawSetCommandsBuffer<IndirectCommand>");
	m_alphaTestDrawSetCommandsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(DispatchMeshIndirectCommand), 1, ResourceState::ALL_SRV, L"alphaTestDrawSetCommandsBuffer<IndirectCommand>");
	m_blendDrawSetCommandsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(DispatchMeshIndirectCommand), 1, ResourceState::ALL_SRV, L"blendDrawSetCommandsBuffer<IndirectCommand>");
	
	m_normalMatrixBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>(ResourceState::ALL_SRV, 1, L"preSkinningNormalMatrixBuffer");

	

	m_activeOpaqueDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeOpaqueDrawSetIndices");
	m_activeAlphaTestDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeAlphaTestDrawSetIndices");
	m_activeBlendDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeBlendDrawSetIndices");
}
void ObjectManager::AddObject(std::shared_ptr<RenderableObject>& object) {
	object->SetCurrentManager(this);
	std::shared_ptr<BufferView> view = m_perObjectBuffers->AddData(&object->GetPerObjectCBData(), sizeof(PerObjectCB), sizeof(PerObjectCB));
	object->SetCurrentPerObjectCBView(view);

	if (object->HasOpaque()) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		// For each mesh, add an indirect command to the draw set buffer
		for (auto& meshInstance : object->GetOpaqueMeshes()) {
			auto& mesh = meshInstance->GetMesh();
			DispatchMeshIndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_opaqueDrawSetCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);
			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			indices.push_back(index);
			m_activeOpaqueDrawSetIndices->Insert(index);
		}

		object->SetCurrentOpaqueDrawSetCommandViews(views);
		object->SetCurrentOpaqueDrawSetIndices(indices);
	}

	if (object->HasAlphaTest()) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		for (auto& meshInstance : object->GetAlphaTestMeshes()) {
			auto& mesh = meshInstance->GetMesh();
			DispatchMeshIndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_alphaTestDrawSetCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);
			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			indices.push_back(index);
			m_activeAlphaTestDrawSetIndices->Insert(index);
		}
		object->SetCurrentAlphaTestDrawSetIndices(indices);
		object->SetCurrentAlphaTestDrawSetCommandViews(views);
	}

	if (object->HasBlend()) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		for (auto& meshInstance : object->GetBlendMeshes()) {
			auto& mesh = meshInstance->GetMesh();
			DispatchMeshIndirectCommand command = {};
			command.perObjectBufferIndex = object->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_blendDrawSetCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);

			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			indices.push_back(index);
			m_activeBlendDrawSetIndices->Insert(index);
		}
		object->SetCurrentBlendDrawSetIndices(indices);
		object->SetCurrentBlendDrawSetCommandViews(views);
	}

	auto normalMatrixView = m_normalMatrixBuffer->Add(DirectX::XMFLOAT4X4());
	object->SetNormalMatrixView(normalMatrixView);
}

void ObjectManager::RemoveObject(std::shared_ptr<RenderableObject>& object) {
	auto& view = object->GetCurrentPerObjectCBView();
	m_perObjectBuffers->Deallocate(view.get());

	DeletionManager::GetInstance().MarkForDelete(view);

	object->SetCurrentManager(nullptr);

	// Remove the object's draw set commands from the draw set buffers
	auto& opaqueViews = object->GetCurrentOpaqueDrawSetCommandViews();
	for (auto view : opaqueViews) {
		m_opaqueDrawSetCommandsBuffer->Deallocate(view.get());
		unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
		m_activeOpaqueDrawSetIndices->Remove(index);
	}
	object->SetCurrentOpaqueDrawSetIndices({});
	object->SetCurrentOpaqueDrawSetCommandViews({});

	auto& transparentViews = object->GetCurrentAlphaTestDrawSetCommandViews();
	for (auto view : transparentViews) {
		m_alphaTestDrawSetCommandsBuffer->Deallocate(view.get());
		unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
		m_activeAlphaTestDrawSetIndices->Remove(index);
	}
	object->SetCurrentAlphaTestDrawSetIndices({});
	object->SetCurrentAlphaTestDrawSetCommandViews({});

	auto& blendViews = object->GetCurrentBlendDrawSetCommandViews();
	for (auto view : blendViews) {
		m_blendDrawSetCommandsBuffer->Deallocate(view.get());
		unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
		m_activeBlendDrawSetIndices->Remove(index);
	}
	object->SetCurrentBlendDrawSetIndices({});

	m_normalMatrixBuffer->Remove(object->GetNormalMatrixView());
}

void ObjectManager::UpdatePerObjectBuffer(BufferView* view, PerObjectCB& data) {
	m_perObjectBuffers->UpdateView(view, &data);
}

void ObjectManager::UpdateNormalMatrixBuffer(BufferView* view, void* data) {
	m_normalMatrixBuffer->UpdateView(view, data);
}