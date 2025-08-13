#include "Managers/ObjectManager.h"

#include <d3d12.h>

#include "Managers/Singletons/ResourceManager.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "Mesh/MeshInstance.h"
#include "Utilities/MathUtils.h"
#include "../shaders/Common/defines.h"
#include "../../generated/BuiltinResources.h"

ObjectManager::ObjectManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_perObjectBuffers = resourceManager.CreateIndexedDynamicBuffer(sizeof(PerObjectCB), 1, L"perObjectBuffers<PerObjectCB>");
	m_masterIndirectCommandsBuffer = resourceManager.CreateIndexedDynamicBuffer(sizeof(DispatchMeshIndirectCommand), 1, L"masterIndirectCommandsBuffer<IndirectCommand>");
	
	m_normalMatrixBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>(1, L"preSkinningNormalMatrixBuffer");

	m_activeOpaqueDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(1, L"activeOpaqueDrawSetIndices");
	m_activeAlphaTestDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(1, L"activeAlphaTestDrawSetIndices");
	m_activeBlendDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(1, L"activeBlendDrawSetIndices");

	m_resources[Builtin::PerObjectBuffer] = m_perObjectBuffers;
	m_resources[Builtin::NormalMatrixBuffer] = m_normalMatrixBuffer;
	m_resources[Builtin::IndirectCommandBuffers::Master] = m_masterIndirectCommandsBuffer;
	m_resources[Builtin::ActiveDrawSetIndices::Opaque] = m_activeOpaqueDrawSetIndices;
	m_resources[Builtin::ActiveDrawSetIndices::AlphaTest] = m_activeAlphaTestDrawSetIndices;
	m_resources[Builtin::ActiveDrawSetIndices::Blend] = m_activeBlendDrawSetIndices;
}
Components::ObjectDrawInfo ObjectManager::AddObject(const PerObjectCB& perObjectCB, const Components::OpaqueMeshInstances* opaqueInstances, const Components::AlphaTestMeshInstances* alphaTestInstances, const Components::BlendMeshInstances* blendInstances) {
	
	Components::ObjectDrawInfo drawInfo;

	std::shared_ptr<BufferView> perObjectCBview = m_perObjectBuffers->AddData(&perObjectCB, sizeof(PerObjectCB), sizeof(PerObjectCB));

	if (opaqueInstances != nullptr) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		// For each mesh, add an indirect command to the draw set buffer
		for (auto& meshInstance : opaqueInstances->meshInstances) {
			auto& mesh = meshInstance->GetMesh();
			DispatchMeshIndirectCommand command = {};
			command.perObjectBufferIndex = static_cast<uint32_t>(perObjectCBview->GetOffset() / sizeof(PerObjectCB));
			command.perMeshBufferIndex = static_cast<uint32_t>(mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
			command.perMeshInstanceBufferIndex = static_cast<uint32_t>(meshInstance->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
			command.dispatchMeshArguments.ThreadGroupCountX = DivRoundUp(mesh->GetMeshletCount(), AS_GROUP_SIZE);
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_masterIndirectCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);
			unsigned int index = static_cast<uint32_t>(view->GetOffset() / sizeof(DispatchMeshIndirectCommand));
			indices.push_back(index);
			m_activeOpaqueDrawSetIndices->Insert(index);
		}

		Components::IndirectDrawInfo info;
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
			command.perObjectBufferIndex = static_cast<uint32_t>(perObjectCBview->GetOffset() / sizeof(PerObjectCB));
			command.perMeshBufferIndex = static_cast<uint32_t>(mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
			command.perMeshInstanceBufferIndex = static_cast<uint32_t>(meshInstance->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
			command.dispatchMeshArguments.ThreadGroupCountX = DivRoundUp(mesh->GetMeshletCount(), AS_GROUP_SIZE);
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_masterIndirectCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);
			unsigned int index = static_cast<uint32_t>(view->GetOffset() / sizeof(DispatchMeshIndirectCommand));
			indices.push_back(index);
			m_activeAlphaTestDrawSetIndices->Insert(index);
		}
		Components::IndirectDrawInfo info;
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
			command.perObjectBufferIndex = static_cast<uint32_t>(perObjectCBview->GetOffset() / sizeof(PerObjectCB));
			command.perMeshBufferIndex = static_cast<uint32_t>(mesh->GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB));
			command.perMeshInstanceBufferIndex = static_cast<uint32_t>(meshInstance->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB));
			command.dispatchMeshArguments.ThreadGroupCountX = DivRoundUp(mesh->GetMeshletCount(), AS_GROUP_SIZE);
			command.dispatchMeshArguments.ThreadGroupCountY = 1;
			command.dispatchMeshArguments.ThreadGroupCountZ = 1;
			std::shared_ptr<BufferView> view = m_masterIndirectCommandsBuffer->AddData(&command, sizeof(DispatchMeshIndirectCommand), sizeof(DispatchMeshIndirectCommand));
			views.push_back(view);

			unsigned int index = static_cast<uint32_t>(view->GetOffset() / sizeof(DispatchMeshIndirectCommand));
			indices.push_back(index);
			m_activeBlendDrawSetIndices->Insert(index);
		}
		Components::IndirectDrawInfo info;
		info.indices = indices;
		info.views = views;
		drawInfo.blend = info;
	}

	auto normalMatrixView = m_normalMatrixBuffer->Add(DirectX::XMFLOAT4X4());
	drawInfo.normalMatrixView = normalMatrixView;
	drawInfo.perObjectCBView = perObjectCBview;
	drawInfo.normalMatrixIndex = normalMatrixView->GetOffset() / sizeof(DirectX::XMFLOAT4X4);
	drawInfo.perObjectCBIndex = perObjectCBview->GetOffset() / sizeof(PerObjectCB);
	return drawInfo;
}

void ObjectManager::RemoveObject(const Components::ObjectDrawInfo* drawInfo) {
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
			unsigned int index = static_cast<uint32_t>(view->GetOffset() / sizeof(DispatchMeshIndirectCommand));
			m_activeOpaqueDrawSetIndices->Remove(index);
		}
	}

	if (drawInfo->alphaTest.has_value()) {
		auto& transparentViews = drawInfo->alphaTest.value().views;
		for (auto view : transparentViews) {
			m_masterIndirectCommandsBuffer->Deallocate(view.get());
			unsigned int index = static_cast<uint32_t>(view->GetOffset() / sizeof(DispatchMeshIndirectCommand));
			m_activeAlphaTestDrawSetIndices->Remove(index);
		}
	}

	if (drawInfo->blend.has_value()) {
		auto& blendViews = drawInfo->blend.value().views;
		for (auto view : blendViews) {
			m_masterIndirectCommandsBuffer->Deallocate(view.get());
			unsigned int index = static_cast<uint32_t>(view->GetOffset() / sizeof(DispatchMeshIndirectCommand));
			m_activeBlendDrawSetIndices->Remove(index);
		}
	}

	m_normalMatrixBuffer->Remove(drawInfo->normalMatrixView.get());
}

void ObjectManager::UpdatePerObjectBuffer(BufferView* view, PerObjectCB& data) {
	std::lock_guard<std::mutex> lock(m_objectUpdateMutex);
	m_perObjectBuffers->UpdateView(view, &data);
}

void ObjectManager::UpdateNormalMatrixBuffer(BufferView* view, void* data) {
	std::lock_guard<std::mutex> lock(m_normalMatrixUpdateMutex);
	m_normalMatrixBuffer->UpdateView(view, data);
}

std::shared_ptr<Resource> ObjectManager::ProvideResource(ResourceIdentifier const& key) {
	return m_resources[key];
}

std::vector<ResourceIdentifier> ObjectManager::GetSupportedKeys() {
	std::vector<ResourceIdentifier> keys;
	keys.reserve(m_resources.size());
	for (auto const& [key, _] : m_resources)
		keys.push_back(key);

	return keys;
}