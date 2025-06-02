#include "Managers/CameraManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Resources/DynamicResource.h"
#include "Resources/ResourceGroup.h"

CameraManager::CameraManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_pCameraBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<CameraInfo>(1, L"cameraBuffer<CameraInfo>");
	m_meshletCullingBitfieldGroup = std::make_shared<ResourceGroup>(L"MeshletCullingBitfieldGroup");
	m_meshInstanceMeshletCullingBitfieldGroup = std::make_shared<ResourceGroup>(L"ObjectCullingBitfieldGroup");
	m_meshInstanceOcclusionCullingBitfieldGroup = std::make_shared<ResourceGroup>(L"MeshInstanceOcclusionCullingBitfieldGroup");
}

Components::RenderView CameraManager::AddCamera(CameraInfo& camera) {
	auto viewID = m_viewIDCounter.fetch_add(1);

	Components::RenderView view;
	view.viewID = viewID;
	view.indirectCommandBuffers = m_pCommandBufferManager->CreateBuffersForView(viewID);
	view.cameraBufferView = m_pCameraBuffer->Add();
	view.cameraBufferIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);

	auto bits = m_currentMeshletBitfieldSize;
	auto words = (bits + 31) / 32;
	auto meshletBitfield = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(words, sizeof(unsigned int), false, true, false);
	meshletBitfield->SetName(L"MeshletBitfieldBuffer (" + std::to_wstring(viewID) + L")");
	view.meshletBitfieldBuffer = std::make_shared<DynamicGloballyIndexedResource>(meshletBitfield);

	bits = m_currentMeshInstanceBitfieldSize;
	auto bytes = (bits + 7) / 8;
	auto meshInstanceMeshletCullingBitfield = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(bytes, sizeof(unsigned int), false, true, false);
	meshInstanceMeshletCullingBitfield->SetName(L"MeshInstanceMeshletCullingBitfieldBuffer (" + std::to_wstring(viewID) + L")");
	view.meshInstanceMeshletCullingBitfieldBuffer = std::make_shared<DynamicGloballyIndexedResource>(meshInstanceMeshletCullingBitfield);

	auto meshInstanceOcclusionCullingBitfield = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(bytes, sizeof(unsigned int), false, true, false);
	meshInstanceOcclusionCullingBitfield->SetName(L"MeshInstanceOcclusionCullingBitfieldBuffer (" + std::to_wstring(viewID) + L")");
	view.meshInstanceOcclusionCullingBitfieldBuffer = std::make_shared<DynamicGloballyIndexedResource>(meshInstanceOcclusionCullingBitfield);

	m_meshletBitfieldBuffers[viewID] = view.meshletBitfieldBuffer;
	m_meshInstanceMeshletCullingBitfieldBuffers[viewID] = view.meshInstanceMeshletCullingBitfieldBuffer;
	m_meshInstanceOcclusionCullingBitfieldBuffers[viewID] = view.meshInstanceOcclusionCullingBitfieldBuffer;

	m_meshletCullingBitfieldGroup->AddResource(view.meshletBitfieldBuffer);
	m_meshInstanceMeshletCullingBitfieldGroup->AddResource(view.meshInstanceMeshletCullingBitfieldBuffer);
	m_meshInstanceOcclusionCullingBitfieldGroup->AddResource(view.meshInstanceOcclusionCullingBitfieldBuffer);

	m_pCameraBuffer->UpdateView(view.cameraBufferView.get(), &camera);

	return view;
}

void CameraManager::RemoveCamera(Components::RenderView view) {
	m_pCameraBuffer->Remove(view.cameraBufferView.get());
	m_meshletBitfieldBuffers.erase(view.viewID);
	m_meshletCullingBitfieldGroup->RemoveResource(view.meshletBitfieldBuffer.get());
	m_meshInstanceMeshletCullingBitfieldGroup->RemoveResource(view.meshInstanceMeshletCullingBitfieldBuffer.get());
}

void CameraManager::SetCommandBufferManager(IndirectCommandBufferManager* commandBufferManager) {
	m_pCommandBufferManager = commandBufferManager;
}

void CameraManager::SetMeshletBitfieldSize(unsigned int numMeshlets) {
	m_currentMeshletBitfieldSize = numMeshlets;
	for (auto& pair : m_meshletBitfieldBuffers) {
		auto& buffer = pair.second;
		DeletionManager::GetInstance().MarkForDelete(buffer->GetResource());
		buffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_currentMeshletBitfieldSize, sizeof(unsigned int), false, true, false));
	}
}

void CameraManager::SetDepthBufferForCamera(Components::RenderView view, Components::DepthMap depth, bool isArray, unsigned int arrayIndex) {

}

void CameraManager::SetNumMeshInstances(unsigned int numMeshInstances) {
	m_currentMeshInstanceBitfieldSize = numMeshInstances;
	for (auto& pair : m_meshInstanceMeshletCullingBitfieldBuffers) {
		auto& buffer = pair.second;
		DeletionManager::GetInstance().MarkForDelete(buffer->GetResource());
		buffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_currentMeshInstanceBitfieldSize, sizeof(unsigned int), false, true, false));
	}

	for (auto& pair : m_meshInstanceOcclusionCullingBitfieldBuffers) {
		auto& buffer = pair.second;
		DeletionManager::GetInstance().MarkForDelete(buffer->GetResource());
		buffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_currentMeshInstanceBitfieldSize, sizeof(unsigned int), false, true, false));
	}
}

std::shared_ptr<Resource> CameraManager::ProvideResource(ResourceIdentifier const& key) {
	switch (key.AsBuiltin()) {
	case BuiltinResource::CameraBuffer:
		return m_pCameraBuffer;
	case BuiltinResource::MeshletCullingBitfieldGroup:
		return m_meshletCullingBitfieldGroup;
	case BuiltinResource::MeshInstanceMeshletCullingBitfieldGroup:
		return m_meshInstanceMeshletCullingBitfieldGroup;
	case BuiltinResource::MeshInstanceOcclusionCullingBitfieldGroup:
		return m_meshInstanceOcclusionCullingBitfieldGroup;
	default:
		spdlog::warn("CameraManager::ProvideResource: Unknown resource key {}", key.ToString());
		return nullptr;
	}
}

std::vector<ResourceIdentifier> CameraManager::GetSupportedKeys() {
	return {
		BuiltinResource::CameraBuffer,
		BuiltinResource::MeshletCullingBitfieldGroup,
		BuiltinResource::MeshInstanceMeshletCullingBitfieldGroup,
		BuiltinResource::MeshInstanceOcclusionCullingBitfieldGroup
	};
}