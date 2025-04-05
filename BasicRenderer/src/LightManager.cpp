#include "LightManager.h"

#include "ResourceHandles.h"
#include "ResourceManager.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "Utilities.h"
#include "SettingsManager.h"
#include "Camera.h"
#include "SceneNode.h"
#include "ShadowMaps.h"
#include "DynamicResource.h"
#include "IndirectCommandBufferManager.h"
#include "MaterialBuckets.h"
#include "DeletionManager.h"
#include "CameraManager.h"
#include "SortedUnsignedIntBuffer.h"

LightManager::LightManager() {
    auto& resourceManager = ResourceManager::GetInstance();

	m_activeLightIndices = resourceManager.CreateIndexedSortedUnsignedIntBuffer(ResourceState::ALL_SRV, 1, L"activeLightIndices");
    m_lightBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<LightInfo>(ResourceState::ALL_SRV, 10, L"lightBuffer<LightInfo>");
    m_spotViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(ResourceState::ALL_SRV, 1, L"spotViewInfo<matrix>");
    m_pointViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(ResourceState::ALL_SRV, 1, L"pointViewInfo<matrix>");
    m_directionalViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<unsigned int>(ResourceState::ALL_SRV, 1, L"direcitonalViewInfo<matrix>");

	getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	getDirectionalLightCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits");
	getShadowResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("shadowResolution");
	getCurrentShadowMapResourceGroup = SettingsManager::GetInstance().getSettingGetter<ShadowMaps*>("currentShadowMapsResourceGroup");
}

LightManager::~LightManager() {
	auto& deletionManager = DeletionManager::GetInstance();
	deletionManager.MarkForDelete(m_lightBuffer);
	deletionManager.MarkForDelete(m_spotViewInfo);
	deletionManager.MarkForDelete(m_pointViewInfo);
	deletionManager.MarkForDelete(m_directionalViewInfo);
}

void LightManager::AddLight(LightInfo* lightInfo, Camera* currentCamera) {

	auto lightBufferView = m_lightBuffer->Add(*lightInfo);
	auto lightIndex = lightBufferView->GetOffset() / sizeof(LightInfo);
	m_activeLightIndices->Insert(lightIndex);

	if (lightInfo->shadowCaster) {
		CreateLightViewInfo(*lightInfo, currentCamera);
		auto shadowMap = getCurrentShadowMapResourceGroup();
		if (shadowMap != nullptr) {
			auto map = shadowMap->AddMap(lightInfo, getShadowResolution());
		}
	}
}

unsigned int LightManager::CreateLightInfo(Light* node) {
    return m_lightBuffer->Size() - 1; // Return new light's index
}

void LightManager::RemoveLight(LightInfo* light) {

}

unsigned int LightManager::GetLightBufferDescriptorIndex() {
    return m_lightBuffer->GetSRVInfo().index;
}

unsigned int LightManager::GetPointCubemapMatricesDescriptorIndex() {
	return m_pointViewInfo->GetSRVInfo().index;
}

unsigned int LightManager::GetSpotMatricesDescriptorIndex() {
	return m_spotViewInfo->GetSRVInfo().index;
}

unsigned int LightManager::GetDirectionalCascadeMatricesDescriptorIndex() {
	return m_directionalViewInfo->GetSRVInfo().index;
}

unsigned int LightManager::GetNumLights() {
    return m_lights.size();
}

Components::LightViewInfo LightManager::CreateLightViewInfo(LightInfo info, Camera* camera) {

    auto projectionMatrix = GetProjectionMatrixForLight(info);

	Components::LightViewInfo viewInfo = {};

	//switch (info.type) {
	//case Components::LightType::Point: {
	//	auto cubeViewIndex = m_pointViewInfo->Size() / 6;
	//	//node->SetLightViewInfoIndex(cubeViewIndex);
	//	auto cubemapMatrices = GetCubemapViewMatrices(node->transform.getGlobalPosition());
	//	for (int i = 0; i < 6; i++) {
	//		CameraInfo info = {};
	//		auto pos = node->transform.getGlobalPosition();
	//		info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
	//		info.view = cubemapMatrices[i];
	//		info.projection = projectionMatrix;
	//		info.viewProjection = XMMatrixMultiply(cubemapMatrices[i], projectionMatrix);
	//		auto view = m_pCameraManager->AddCamera(info);
	//		viewInfo.cameraBufferViews.push_back(view);
	//		m_pointViewInfo->Add(view->GetOffset()/sizeof(CameraInfo));
	//		viewInfo.commandBuffers.opaqueIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::Opaque));
	//		viewInfo.commandBuffers.alphaTestIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::AlphaTest));
	//		viewInfo.commandBuffers.blendIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::Blend));
	//	}
	//	break;
	//}
	//case Components::LightType::Spot: {
	//	//node->SetLightViewInfoIndex(m_spotViewInfo->Size());
	//	CameraInfo camera = {};
	//	auto pos = node->transform.getGlobalPosition();
	//	camera.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
	//	camera.view = node->GetLightViewMatrix();
	//	camera.projection = projectionMatrix;
	//	camera.viewProjection = XMMatrixMultiply(node->GetLightViewMatrix(), projectionMatrix);
	//	auto view = m_pCameraManager->AddCamera(camera);
	//	viewInfo.cameraBufferViews.push_back(view);
	//	m_spotViewInfo->Add(view->GetOffset() / sizeof(CameraInfo));
	//	viewInfo.commandBuffers.opaqueIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::Opaque));
	//	viewInfo.commandBuffers.alphaTestIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::AlphaTest));
	//	viewInfo.commandBuffers.blendIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::Blend));
	//	break;
	//}
	//case Components::LightType::Directional: {
	//	auto numCascades = getNumDirectionalLightCascades();
	//	//node->SetLightViewInfoIndex(m_directionalViewInfo->Size() / numCascades);
	//	auto cascades = setupCascades(numCascades, *node, *camera, getDirectionalLightCascadeSplits());
	//	std::vector<std::shared_ptr<BufferView>> views;
	//	for (int i = 0; i < numCascades; i++) {
	//		CameraInfo info = {};
	//		auto pos = node->transform.getGlobalPosition();
	//		info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
	//		info.view = cascades[i].viewMatrix;
	//		info.projection = cascades[i].orthoMatrix;
	//		info.viewProjection = XMMatrixMultiply(cascades[i].viewMatrix, cascades[i].orthoMatrix);
	//		auto view = m_pCameraManager->AddCamera(info);
	//		viewInfo.cameraBufferViews.push_back(view);
	//		m_directionalViewInfo->Add(view->GetOffset() / sizeof(CameraInfo));
	//		viewInfo.commandBuffers.opaqueIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::Opaque));
	//		viewInfo.commandBuffers.alphaTestIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::AlphaTest));
	//		viewInfo.commandBuffers.blendIndirectCommandBuffers.push_back(m_pCommandBufferManager->CreateBuffer(node->GetLocalID(), MaterialBuckets::Blend));
	//	}
	//	break;
	//}
	//}
	return viewInfo;
}

void LightManager::UpdateLightViewInfo(Light* light) {
	auto projectionMatrix = light->GetLightProjectionMatrix();
	auto& views = light->GetCameraBufferViews();
	switch (light->GetLightType()) {
	case Components::LightType::Point: {
		auto cubemapMatrices = GetCubemapViewMatrices(light->transform.getGlobalPosition());
		auto planes = light->GetFrustumPlanes();
		for (int i = 0; i < 6; i++) {
			CameraInfo info = {};
			auto pos = light->transform.getGlobalPosition();
			info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
			info.view = cubemapMatrices[i];
			info.projection = projectionMatrix;
			info.viewProjection = XMMatrixMultiply(cubemapMatrices[i], projectionMatrix);
			info.clippingPlanes[0] = planes[i][0];
			info.clippingPlanes[1] = planes[i][1];
			info.clippingPlanes[2] = planes[i][2];
			info.clippingPlanes[3] = planes[i][3];
			info.clippingPlanes[4] = planes[i][4];
			info.clippingPlanes[5] = planes[i][5];
			m_pCameraManager->UpdateCamera(views[i], info);
		}
		break;
	}
	case Components::LightType::Spot: {
		CameraInfo camera = {};
		auto pos = light->transform.getGlobalPosition();
		camera.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
		camera.view = light->GetLightViewMatrix();
		camera.projection = projectionMatrix;
		camera.viewProjection = XMMatrixMultiply(light->GetLightViewMatrix(), projectionMatrix);
		auto planes = light->GetFrustumPlanes();
		camera.clippingPlanes[0] = planes[0][0];
		camera.clippingPlanes[1] = planes[0][1];
		camera.clippingPlanes[2] = planes[0][2];
		camera.clippingPlanes[3] = planes[0][3];
		camera.clippingPlanes[4] = planes[0][4];
		camera.clippingPlanes[5] = planes[0][5];
		m_pCameraManager->UpdateCamera(views[0], camera);
		break;
	}
	case Components::LightType::Directional: {
		if (m_currentCamera == nullptr) {
			spdlog::warn("Camera must be provided for directional light shadow mapping");
			return;
		}
		auto numCascades = getNumDirectionalLightCascades();
		auto cascades = setupCascades(numCascades, *light, *m_currentCamera, getDirectionalLightCascadeSplits());
		for (int i = 0; i < numCascades; i++) {
			CameraInfo info = {};
			auto pos = light->transform.getGlobalPosition();
			info.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
			info.view = cascades[i].viewMatrix;
			info.projection = cascades[i].orthoMatrix;
			info.viewProjection = XMMatrixMultiply(cascades[i].viewMatrix, cascades[i].orthoMatrix);
			info.clippingPlanes[0] = cascades[i].frustumPlanes[0];
			info.clippingPlanes[1] = cascades[i].frustumPlanes[1];
			info.clippingPlanes[2] = cascades[i].frustumPlanes[2];
			info.clippingPlanes[3] = cascades[i].frustumPlanes[3];
			info.clippingPlanes[4] = cascades[i].frustumPlanes[4];
			info.clippingPlanes[5] = cascades[i].frustumPlanes[5];
			m_pCameraManager->UpdateCamera(views[i], info);
		}
		break;
	}
	default:
		spdlog::warn("Light type not recognized");
	}
}

void LightManager::RemoveLightViewInfo(Light* node) {
	int viewInfoIndex = node->GetCurrentviewInfoIndex();
	if (viewInfoIndex < 0) { // Not a shadow caster
		return;
	}
	m_pCommandBufferManager->UnregisterBuffers(node->GetLocalID()); // Remove indirect command buffers
	node->DeleteAllIndirectCommandBuffers();
	switch (node->GetLightType()) {
	case Components::LightType::Point: {
		// Erase light in point lights
		m_pointLights.erase(m_pointLights.begin() + viewInfoIndex);
		// Erase view info in structured buffer
		auto& views = node->GetCameraBufferViews();
		for (int i = 0; i < 6; i++) {
			m_pCameraManager->RemoveCamera(views[i]);
		}
		// Update subsequent view info indices
		for (int i = viewInfoIndex; i < m_pointLights.size(); i++) {
			m_pointLights[i]->DecrementLightViewInfoIndex();
		}
		break;
	}
	case Components::LightType::Spot: {
		// Erase light in spot lights
		m_spotLights.erase(m_spotLights.begin() + viewInfoIndex);
		// Erase view info in structured buffer
		m_pCameraManager->RemoveCamera(node->GetCameraBufferViews()[0]);
		// Update subsequent view info indices
		for (int i = viewInfoIndex; i < m_spotLights.size(); i++) {
			m_spotLights[i]->DecrementLightViewInfoIndex();
		}
		break;
	}
	case Components::LightType::Directional: {
		// Erase light in directional lights
		m_directionalLights.erase(m_directionalLights.begin() + viewInfoIndex);
		// Erase view info in structured buffer
		auto& views = node->GetCameraBufferViews();
		for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
			m_pCameraManager->RemoveCamera(views[i]);
		}
		// Update subsequent view info indices
		for (int i = viewInfoIndex; i < m_directionalLights.size(); i++) {
			m_directionalLights[i]->DecrementLightViewInfoIndex();
		}
		break;
	}
	default:
		spdlog::warn("Light type not recognized");
	}
}

void LightManager::SetCurrentCamera(Camera* camera) {
	if (m_currentCamera != nullptr) {
		//m_currentCamera->RemoveObserver(this);
	}
	m_currentCamera = camera;
	//m_currentCamera->AddObserver(this);
}

void LightManager::SetCommandBufferManager(IndirectCommandBufferManager* commandBufferManager) {
	m_pCommandBufferManager = commandBufferManager;
}

void LightManager::SetCameraManager(CameraManager* cameraManager) {
	m_pCameraManager = cameraManager;
}