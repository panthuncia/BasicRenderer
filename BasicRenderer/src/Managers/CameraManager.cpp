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
	m_meshInstanceCullingBitfieldGroup = std::make_shared<ResourceGroup>(L"ObjectCullingBitfieldGroup");
}

Components::RenderView CameraManager::AddCamera(CameraInfo& camera) {
	auto viewID = m_viewIDCounter.fetch_add(1);

	Components::RenderView view;
	view.viewID = viewID;
	view.indirectCommandBuffers = m_pCommandBufferManager->CreateBuffersForView(viewID);
	view.cameraBufferView = m_pCameraBuffer->Add();
	view.cameraBufferIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);

	auto bits = m_currentMeshletBitfieldSize;
	auto bytes = (bits + 7) / 8;
	auto meshletBitfield = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(bytes, sizeof(unsigned int), false, true, false);
	meshletBitfield->SetName(L"MeshletBitfieldBuffer (" + std::to_wstring(viewID) + L")");
	view.meshletBitfieldBuffer = std::make_shared<DynamicGloballyIndexedResource>(meshletBitfield);

	bits = m_currentMeshInstanceBitfieldSize;
	bytes = (bits + 7) / 8;
	auto meshInstanceBitfield = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(bytes, sizeof(unsigned int), false, true, false);

	meshInstanceBitfield->SetName(L"ObjectBitfieldBuffer (" + std::to_wstring(viewID) + L")");
	view.meshInstanceBitfieldBuffer = std::make_shared<DynamicGloballyIndexedResource>(meshInstanceBitfield);

	m_meshletBitfieldBuffers[viewID] = view.meshletBitfieldBuffer;
	m_meshInstanceBitfieldBuffers[viewID] = view.meshInstanceBitfieldBuffer;

	m_meshletCullingBitfieldGroup->AddResource(view.meshletBitfieldBuffer);
	m_meshInstanceCullingBitfieldGroup->AddResource(view.meshInstanceBitfieldBuffer);

	m_pCameraBuffer->UpdateView(view.cameraBufferView.get(), &camera);

	return view;
}

void CameraManager::RemoveCamera(Components::RenderView view) {
	m_pCameraBuffer->Remove(view.cameraBufferView.get());
	m_meshletBitfieldBuffers.erase(view.viewID);
	m_meshletCullingBitfieldGroup->RemoveResource(view.meshletBitfieldBuffer.get());
}

void CameraManager::SetCommandBufferManager(IndirectCommandBufferManager* commandBufferManager) {
	m_pCommandBufferManager = commandBufferManager;
}

void CameraManager::SetMeshletBitfieldSize(unsigned int numMeshlets) {
	m_currentMeshletBitfieldSize = numMeshlets;
	for (auto& pair : m_meshletBitfieldBuffers) {
		auto& buffer = pair.second;
		buffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_currentMeshletBitfieldSize, sizeof(unsigned int), false, true, false));
	}
}

void CameraManager::SetDepthBufferForCamera(Components::RenderView view, Components::DepthMap depth, bool isArray, unsigned int arrayIndex) {

}

void CameraManager::SetNumMeshInstances(unsigned int numMeshInstances) {
	m_currentMeshInstanceBitfieldSize = numMeshInstances;
	for (auto& pair : m_meshInstanceBitfieldBuffers) {
		auto& buffer = pair.second;
		buffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_currentMeshInstanceBitfieldSize, sizeof(unsigned int), false, true, false));
	}
}