#include "CameraManager.h"

#include "ResourceManager.h"

CameraManager::CameraManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_pCameraBuffer = resourceManager.CreateIndexedDynamicStructuredBuffer<CameraInfo>(ResourceState::ALL_SRV, 1, L"cameraBuffer<CameraInfo>");
}

std::shared_ptr<BufferView> CameraManager::AddCamera(CameraInfo& camera) {
	//std::unique_ptr<BufferView> view = m_pCameraBuffer->Add();
	//m_pCameraBuffer->UpdateAt(view, camera->GetCameraInfo());
	//camera->SetCurrentCameraCBView(std::move(view));

	//auto& manager = ResourceManager::GetInstance();
	//manager.QueueDynamicBufferUpdate(m_pCameraBuffer.get());
	return nullptr;
}