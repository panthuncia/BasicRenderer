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
	m_masterIndirectCommandsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(DispatchMeshIndirectCommand), 1, ResourceState::ALL_SRV, L"masterIndirectCommandsBuffer<IndirectCommand>");
	
	m_normalMatrixBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>(ResourceState::ALL_SRV, 1, L"preSkinningNormalMatrixBuffer");

	m_activeOpaqueDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeOpaqueDrawSetIndices");
	m_activeAlphaTestDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeAlphaTestDrawSetIndices");
	m_activeBlendDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeBlendDrawSetIndices");
}
ObjectDrawInfo ObjectManager::AddObject(PerObjectCB& perObjectCB, const Components::OpaqueMeshInstances* opaqueInstances, const Components::AlphaTestMeshInstances* alphaTestInstances, const Components::BlendMeshInstances* blendInstances) {
	
	ObjectDrawInfo drawInfo;

	std::shared_ptr<BufferView> perObjectCBview = m_perObjectBuffers->AddData(&perObjectCB, sizeof(PerObjectCB), sizeof(PerObjectCB));

	if (opaqueInstances != nullptr) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		// For each mesh, add an indirect command to the draw set buffer
		for (auto& meshInstance : opaqueInstances->meshInstances) {
			auto& mesh = meshInstance->GetMesh();
			DispatchMeshIndirectCommand command = {};
			command.perObjectBufferIndex = perObjectCBview->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.perMeshInstanceBufferIndex = meshInstance->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_masterIndirectCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);
			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			indices.push_back(index);
			m_activeOpaqueDrawSetIndices->Insert(index);
		}

		IndirectDrawInfo info;
		info.indices = indices;
		info.views = views;
		drawInfo.opaque = info;
	}

	if (alphaTestInstances != nullptr) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		for (auto& meshInstance : alphaTestInstances->meshInstances) {
			auto& mesh = meshInstance->GetMesh();
			DispatchMeshIndirectCommand command = {};
			command.perObjectBufferIndex = perObjectCBview->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.perMeshInstanceBufferIndex = meshInstance->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_masterIndirectCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);
			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			indices.push_back(index);
			m_activeAlphaTestDrawSetIndices->Insert(index);
		}
		IndirectDrawInfo info;
		info.indices = indices;
		info.views = views;
		drawInfo.alphaTest = info;
	}

	if (blendInstances != nullptr) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		for (auto& meshInstance : blendInstances->meshInstances) {
			auto& mesh = meshInstance->GetMesh();
			DispatchMeshIndirectCommand command = {};
			command.perObjectBufferIndex = perObjectCBview->GetOffset() / sizeof(PerObjectCB);
			command.perMeshBufferIndex = mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
			command.perMeshInstanceBufferIndex = meshInstance->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
			command.dispatchMeshArguments.ThreadGroupCountX = mesh->GetMeshletCount();
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_masterIndirectCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);

			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			indices.push_back(index);
			m_activeBlendDrawSetIndices->Insert(index);
		}
		IndirectDrawInfo info;
		info.indices = indices;
		info.views = views;
		drawInfo.blend = info;
	}

	auto normalMatrixView = m_normalMatrixBuffer->Add(DirectX::XMFLOAT4X4());
	drawInfo.normalMatrixView = normalMatrixView;
	return drawInfo;
}

void ObjectManager::RemoveObject(const ObjectDrawInfo* drawInfo) {
#ifdef _DEBUG
	if (drawInfo == nullptr) {
		throw std::runtime_error("ObjectDrawInfo is null");
		return;
	}
#endif // _DEBUG

	m_perObjectBuffers->Deallocate(drawInfo->perObjectCBView.get());

	DeletionManager::GetInstance().MarkForDelete(drawInfo->perObjectCBView);

	// Remove the object's draw set commands from the draw set buffers
	if (drawInfo->opaque.has_value()) {
		auto& opaqueViews = drawInfo->opaque.value().views;
		for (auto view : opaqueViews) {
			m_masterIndirectCommandsBuffer->Deallocate(view.get());
			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			m_activeOpaqueDrawSetIndices->Remove(index);
		}
	}

	if (drawInfo->alphaTest.has_value()) {
		auto& transparentViews = drawInfo->alphaTest.value().views;
		for (auto view : transparentViews) {
			m_masterIndirectCommandsBuffer->Deallocate(view.get());
			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			m_activeAlphaTestDrawSetIndices->Remove(index);
		}
	}

	if (drawInfo->blend.has_value()) {
		auto& blendViews = drawInfo->blend.value().views;
		for (auto view : blendViews) {
			m_masterIndirectCommandsBuffer->Deallocate(view.get());
			unsigned int index = view->GetOffset() / sizeof(DispatchMeshIndirectCommand);
			m_activeBlendDrawSetIndices->Remove(index);
		}
	}

	m_normalMatrixBuffer->Remove(drawInfo->normalMatrixView.get());
}

void ObjectManager::UpdatePerObjectBuffer(BufferView* view, PerObjectCB& data) {
	m_perObjectBuffers->UpdateView(view, &data);
}

void ObjectManager::UpdateNormalMatrixBuffer(BufferView* view, void* data) {
	m_normalMatrixBuffer->UpdateView(view, data);
}

unsigned int ObjectManager::GetPerObjectBufferSRVIndex() const {
	return m_perObjectBuffers->GetSRVInfo().index;
}

unsigned int ObjectManager::GetMasterIndirectCommandsBufferSRVIndex() const {
	return m_masterIndirectCommandsBuffer->GetSRVInfo().index;
}

unsigned int ObjectManager::GetActiveOpaqueDrawSetIndicesBufferSRVIndex() const {
	return m_activeOpaqueDrawSetIndices->GetSRVInfo().index;
}

unsigned int ObjectManager::GetActiveAlphaTestDrawSetIndicesBufferSRVIndex() const {
	return m_activeAlphaTestDrawSetIndices->GetSRVInfo().index;
}

unsigned int ObjectManager::GetActiveBlendDrawSetIndicesBufferSRVIndex() const {
	return m_activeBlendDrawSetIndices->GetSRVInfo().index;
}

unsigned int ObjectManager::GetNormalMatrixBufferSRVIndex() const {
	return m_normalMatrixBuffer->GetSRVInfo().index;
}