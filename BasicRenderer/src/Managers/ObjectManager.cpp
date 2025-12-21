#include "Managers/ObjectManager.h"


#include <DirectXMath.h>

#include "Managers/Singletons/ResourceManager.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "Mesh/MeshInstance.h"
#include "Utilities/MathUtils.h"
#include "../shaders/Common/defines.h"
#include "../../generated/BuiltinResources.h"
#include "Materials/Material.h"
#include "Resources/components.h"

ObjectManager::ObjectManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_perObjectBuffers = DynamicBuffer::CreateShared(sizeof(PerObjectCB), 1, L"perObjectBuffers<PerObjectCB>");
	m_masterIndirectCommandsBuffer = DynamicBuffer::CreateShared(sizeof(DispatchMeshIndirectCommand), 1, L"masterIndirectCommandsBuffer<IndirectCommand>");

	m_normalMatrixBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>(1, L"preSkinningNormalMatrixBuffer");

	//m_activeDrawSetIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(1, L"activeOpaqueDrawSetIndices");

	m_resources[Builtin::PerObjectBuffer] = m_perObjectBuffers;
	m_resources[Builtin::NormalMatrixBuffer] = m_normalMatrixBuffer;
	m_resources[Builtin::IndirectCommandBuffers::Master] = m_masterIndirectCommandsBuffer;
	//m_resources[Builtin::ActiveDrawSetIndices::Opaque] = m_activeOpaqueDrawSetIndices;
	//m_resources[Builtin::ActiveDrawSetIndices::AlphaTest] = m_activeAlphaTestDrawSetIndices;
	//m_resources[Builtin::ActiveDrawSetIndices::Blend] = m_activeBlendDrawSetIndices;
}
Components::ObjectDrawInfo ObjectManager::AddObject(const PerObjectCB& perObjectCB, const Components::MeshInstances* meshInstances) {

	Components::ObjectDrawInfo drawInfo;

	std::shared_ptr<BufferView> perObjectCBview = m_perObjectBuffers->AddData(&perObjectCB, sizeof(PerObjectCB), sizeof(PerObjectCB));

	// Patch all mesh instances with their perObject index
	uint32_t perObjectIndex = static_cast<uint32_t>(perObjectCBview->GetOffset() / sizeof(PerObjectCB));
	if (meshInstances) {
		for (auto& inst : meshInstances->meshInstances) {
			inst->SetPerObjectBufferIndex(perObjectIndex);
		}
	}

	if (meshInstances != nullptr) {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		std::vector<MaterialCompileFlags> materialTechniques;
		// For each mesh, add an indirect command to the draw set buffer
		for (auto& meshInstance : meshInstances->meshInstances) {
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
			auto materialFlags = meshInstance->GetMesh()->material->Technique().compileFlags;
			if (!m_activeDrawSetIndices.contains(materialFlags)) {
				m_activeDrawSetIndices[materialFlags] = ResourceManager::GetInstance().CreateIndexedSortedUnsignedIntBuffer(1, L"activeDrawSetIndices(flags=" + std::to_wstring(static_cast<uint64_t>(materialFlags)) + L")");
				auto& buf = m_activeDrawSetIndices[materialFlags];
				buf->GetECSEntity().add<Components::IsActiveDrawSetIndices>();
				buf->GetECSEntity().set<Components::Resource>({ buf });
				for (auto& phase : meshInstance->GetMesh()->material->Technique().passes) {
					buf->GetECSEntity().add<Components::ParticipatesInPass>(ECSManager::GetInstance().GetRenderPhaseEntity(phase));
				}
			}
			m_activeDrawSetIndices[materialFlags]->Insert(index);
			materialTechniques.push_back(materialFlags);
		}

		Components::IndirectDrawInfo info;
		info.indices = indices;
		info.views = views;
		info.materialTechniques = materialTechniques;
		drawInfo.drawInfo = info;
	}

	auto normalMatrixView = m_normalMatrixBuffer->Add(DirectX::XMFLOAT4X4());
	drawInfo.normalMatrixView = normalMatrixView;
	drawInfo.perObjectCBView = perObjectCBview;
	drawInfo.normalMatrixIndex = static_cast<uint32_t>(normalMatrixView->GetOffset() / sizeof(DirectX::XMFLOAT4X4));
	drawInfo.perObjectCBIndex = static_cast<uint32_t>(perObjectCBview->GetOffset() / sizeof(PerObjectCB));
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

	// Remove the object's draw set commands from the draw set buffers
	auto& views = drawInfo;
	unsigned int i = 0;
	for (auto view : views->drawInfo.views) {
		m_masterIndirectCommandsBuffer->Deallocate(view.get());
		unsigned int index = static_cast<uint32_t>(view->GetOffset() / sizeof(DispatchMeshIndirectCommand));
		m_activeDrawSetIndices[views->drawInfo.materialTechniques[i]]->Remove(index);
		i++;
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