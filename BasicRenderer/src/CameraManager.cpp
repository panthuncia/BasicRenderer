#include "CameraManager.h"

#include "ResourceManager.h"

CameraManager::CameraManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_pCameraBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<CameraInfo>(ResourceState::ALL_SRV, 1, L"cameraBuffer<CameraInfo>");
}

std::shared_ptr<BufferView> CameraManager::AddCamera(CameraInfo& camera) {
	std::shared_ptr<BufferView> view = m_pCameraBuffer->Add();
	m_pCameraBuffer->UpdateView(view.get(), &camera);
	return view;
}

void CameraManager::RemoveCamera(std::shared_ptr<BufferView> view) {
	m_pCameraBuffer->Remove(view.get());
}