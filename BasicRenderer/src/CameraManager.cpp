#include "CameraManager.h"

#include "ResourceManager.h"
#include "SettingsManager.h"

CameraManager::CameraManager() {
	uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();

	auto& resourceManager = ResourceManager::GetInstance();
	m_pCameraBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<CameraInfo>(ResourceState::ALL_SRV, 1, numFramesInFlight, L"cameraBuffer<CameraInfo>");
}

std::shared_ptr<BufferView> CameraManager::AddCamera(CameraInfo& camera) {
	std::shared_ptr<BufferView> view = m_pCameraBuffer->Add();
	m_pCameraBuffer->UpdateView(view.get(), &camera);
	return view;
}

void CameraManager::RemoveCamera(std::shared_ptr<BufferView> view) {
	m_pCameraBuffer->Remove(view.get());
}