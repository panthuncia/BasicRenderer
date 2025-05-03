#include "Managers/CameraManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/IndirectCommandBufferManager.h"

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

	m_pCameraBuffer->UpdateView(view.cameraBufferView.get(), &camera);

	return view;
}

void CameraManager::RemoveCamera(Components::RenderView view) {
	m_pCameraBuffer->Remove(view.cameraBufferView.get());
}

void CameraManager::SetCommandBufferManager(IndirectCommandBufferManager* commandBufferManager) {
	m_pCommandBufferManager = commandBufferManager;
}