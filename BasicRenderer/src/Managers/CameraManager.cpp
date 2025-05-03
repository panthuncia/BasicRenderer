#include "Managers/CameraManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Resources/DynamicResource.h"

CameraManager::CameraManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_pCameraBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<CameraInfo>(1, L"cameraBuffer<CameraInfo>");
}

Components::RenderView CameraManager::AddCamera(CameraInfo& camera) {
	auto viewID = m_viewIDCounter.fetch_add(1);

	Components::RenderView view;
	view.viewID = viewID;
	view.indirectCommandBuffers = m_pCommandBufferManager->CreateBuffersForView(viewID);
	view.cameraBufferView = m_pCameraBuffer->Add();
	view.cameraBufferIndex = view.cameraBufferView->GetOffset() / sizeof(CameraInfo);
	auto bitfield = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_currentMeshletBitfieldSize, sizeof(unsigned int), false, true, false);
	view.meshletBitfieldBufferView = std::make_shared<DynamicGloballyIndexedResource>(bitfield);

	m_meshletBitfieldBuffers[viewID] = view.meshletBitfieldBufferView;

	m_pCameraBuffer->UpdateView(view.cameraBufferView.get(), &camera);

	return view;
}

void CameraManager::RemoveCamera(Components::RenderView view) {
	m_pCameraBuffer->Remove(view.cameraBufferView.get());
	m_meshletBitfieldBuffers.erase(view.viewID);
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