#include "LightManager.h"

#include "ResourceHandles.h"
#include "ResourceManager.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "Utilities.h"
#include "SettingsManager.h"
#include "Light.h"
#include "Camera.h"
#include "SceneNode.h"
#include "ShadowMaps.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "DynamicResource.h"

LightManager::LightManager() {
    auto& resourceManager = ResourceManager::GetInstance();
    m_lightBuffer = resourceManager.CreateIndexedDynamicStructuredBuffer<LightInfo>(ResourceState::ALL_SRV, 1, L"lightBuffer<LightInfo>");
    m_spotViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(ResourceState::ALL_SRV, 1, L"spotViewInfo<matrix>");
    m_pointViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(ResourceState::ALL_SRV, 1, L"pointViewInfo<matrix>");
    m_directionalViewInfo = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(ResourceState::ALL_SRV, 1, L"direcitonalViewInfo<matrix>");
	getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
	getDirectionalLightCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits");
	getShadowResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("shadowResolution");
	getCurrentShadowMapResourceGroup = SettingsManager::GetInstance().getSettingGetter<ShadowMaps*>("currentShadowMapsResourceGroup");
}

void LightManager::Initialize() {
	
}

void LightManager::AddLight(Light* lightNode, bool shadowCasting, Camera* currentCamera) {
    if (lightNode->GetCurrentLightBufferIndex() != -1) {
        RemoveLight(lightNode);
    }
    unsigned int index = CreateLightInfo(lightNode);
    m_lights.push_back(lightNode);
    switch (lightNode->GetLightType()) {
	case LightType::Spot:
		m_spotLights.push_back(lightNode);
		break;
	case LightType::Point:
		m_pointLights.push_back(lightNode);
		break;
	case LightType::Directional:
		m_directionalLights.push_back(lightNode);
		break;
    }
    lightNode->SetLightBufferIndex(index);
	if (shadowCasting) {
		CreateIndirectCommandBuffer(lightNode);
		CreateLightViewInfo(lightNode, currentCamera);
		auto shadowMap = getCurrentShadowMapResourceGroup();
		if (shadowMap != nullptr) {
			shadowMap->AddMap(lightNode, getShadowResolution());
		}
	}
    lightNode->AddLightObserver(this);
}

unsigned int LightManager::CreateLightInfo(Light* node) {
    m_lightBuffer->Add(node->GetLightInfo());
    return m_lightBuffer->Size() - 1; // Return new light's index
}

void LightManager::RemoveLight(Light* light) {
    int index = light->GetCurrentLightBufferIndex();
    if (m_lights[index] != light) {
        spdlog::warn("Light requested for removal does not belong to this scene");
		return;
    }
    
    for (int i = index; i < m_lights.size(); i++) {
        m_lights[i]->DecrementLightBufferIndex();
    }

    m_lights.erase(m_lights.begin() + index);

    m_lightBuffer->RemoveAt(index);
	light->RemoveLightObserver(this);
	light->SetLightBufferIndex(-1);
	RemoveLightViewInfo(light);

	m_lightDrawSetBufferMap.erase(light->GetLocalID());
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

void LightManager::CreateIndirectCommandBuffer(Light* light) {
	auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer<IndirectCommand>(m_commandBufferSize, ResourceState::UNORDERED_ACCESS, false, true);
	m_lightDrawSetBufferMap.emplace(
		light->GetLocalID(),
		DynamicGloballyIndexedResource(resource.dataBuffer)
	);
}

unsigned int LightManager::CreateLightViewInfo(Light* node, Camera* camera) {
    auto projectionMatrix = node->GetLightProjectionMatrix();
	switch (node->GetLightType()) {
	case LightType::Point: {
		auto cubeViewIndex = m_pointViewInfo->Size() / 6;
		node->SetLightViewInfoIndex(cubeViewIndex);
		auto cubemapMatrices = GetCubemapViewMatrices(node->transform.getGlobalPosition());
		for (int i = 0; i < 6; i++) {
			m_pointViewInfo->Add(XMMatrixMultiply(cubemapMatrices[i], projectionMatrix));
		}
		break;
	}
	case LightType::Spot: {
		node->SetLightViewInfoIndex(m_spotViewInfo->Size());
		m_spotViewInfo->Add(XMMatrixMultiply(node->GetLightViewMatrix(), projectionMatrix));
		break;
	}
	case LightType::Directional: {
		if (camera == nullptr) {
			spdlog::warn("Camera must be provided for directional light shadow mapping");
			return -1;
		}
		auto numCascades = getNumDirectionalLightCascades();
		node->SetLightViewInfoIndex(m_directionalViewInfo->Size() / numCascades);
		auto cascades = setupCascades(numCascades, *node, *camera, getDirectionalLightCascadeSplits());
		for (int i = 0; i < numCascades; i++) {
			m_directionalViewInfo->Add(XMMatrixMultiply(cascades[i].orthoMatrix, cascades[i].viewMatrix));
		}
		break;
	}
	default:
		spdlog::warn("Light type not recognized");
		return -1;
	}
}

void LightManager::UpdateLightViewInfo(Light* light) {
	auto projectionMatrix = light->GetLightProjectionMatrix();
	switch (light->GetLightType()) {
	case LightType::Point: {
		auto cubemapMatrices = GetCubemapViewMatrices(light->transform.getGlobalPosition());
		for (int i = 0; i < 6; i++) {
			m_pointViewInfo->UpdateAt(light->GetCurrentviewInfoIndex()*6+i,XMMatrixMultiply(cubemapMatrices[i], projectionMatrix));
		}
		break;
	}
	case LightType::Spot: {
		m_spotViewInfo->UpdateAt(light->GetCurrentviewInfoIndex(), XMMatrixMultiply(light->GetLightViewMatrix(), projectionMatrix));
		break;
	}
	case LightType::Directional: {
		if (m_currentCamera == nullptr) {
			spdlog::warn("Camera must be provided for directional light shadow mapping");
			return;
		}
		auto numCascades = getNumDirectionalLightCascades();
		auto cascades = setupCascades(numCascades, *light, *m_currentCamera, getDirectionalLightCascadeSplits());
		for (int i = 0; i < numCascades; i++) {
			m_directionalViewInfo->UpdateAt(light->GetCurrentviewInfoIndex()*numCascades+i, XMMatrixMultiply(cascades[i].viewMatrix, cascades[i].orthoMatrix));
		}
		break;
	}
	default:
		spdlog::warn("Light type not recognized");
	}
}

void LightManager::RemoveLightViewInfo(Light* node) {
	int viewInfoIndex = node->GetCurrentviewInfoIndex();
	switch (node->GetLightType()) {
	case LightType::Point:
		// Erase light in point lights
		m_pointLights.erase(m_pointLights.begin() + viewInfoIndex);
		// Erase view info in structured buffer
		for (int i = 0; i < 6; i++) {
			m_pointViewInfo->RemoveAt(viewInfoIndex);
		}
		// Update subsequent view info indices
		for (int i = viewInfoIndex; i < m_pointLights.size(); i++) {
			m_pointLights[i]->DecrementLightViewInfoIndex();
		}
		break;
	case LightType::Spot:
		// Erase light in spot lights
		m_spotLights.erase(m_spotLights.begin() + viewInfoIndex);
		// Erase view info in structured buffer
		m_spotViewInfo->RemoveAt(node->GetCurrentviewInfoIndex());
		// Update subsequent view info indices
		for (int i = viewInfoIndex; i < m_spotLights.size(); i++) {
			m_spotLights[i]->DecrementLightViewInfoIndex();
		}
		break;
	case LightType::Directional:
		// Erase light in directional lights
		m_directionalLights.erase(m_directionalLights.begin() + viewInfoIndex);
		// Erase view info in structured buffer
		for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
			m_directionalViewInfo->RemoveAt(viewInfoIndex);
		}
		// Update subsequent view info indices
		for (int i = viewInfoIndex; i < m_directionalLights.size(); i++) {
			m_directionalLights[i]->DecrementLightViewInfoIndex();
		}
		break;
	default:
		spdlog::warn("Light type not recognized");
	}
}

void LightManager::SetCurrentCamera(Camera* camera) {
	if (m_currentCamera != nullptr) {
		m_currentCamera->RemoveObserver(this);
	}
	m_currentCamera = camera;
	m_currentCamera->AddObserver(this);
}

void LightManager::OnNodeUpdated(SceneNode* camera) {
	for (Light* light : m_directionalLights) {
		if (light->GetLightInfo().shadowMapIndex != -1) {
			UpdateLightViewInfo(light);
		}
	}
}
void LightManager::OnNodeUpdated(Light* light) {
	m_lightBuffer->UpdateAt(light->GetCurrentLightBufferIndex(), light->GetLightInfo());
	if (light->GetLightInfo().shadowMapIndex != -1) {
		UpdateLightViewInfo(light);
	}
}

void LightManager::UpdateBuffers() {
	if (m_lightBuffer->UpdateUploadBuffer())
		ResourceManager::GetInstance().QueueDynamicBufferUpdate(m_lightBuffer.get());
	if (m_spotViewInfo->UpdateUploadBuffer())
		ResourceManager::GetInstance().QueueDynamicBufferUpdate(m_spotViewInfo.get());
	if (m_pointViewInfo->UpdateUploadBuffer())
		ResourceManager::GetInstance().QueueDynamicBufferUpdate(m_pointViewInfo.get());
	if (m_directionalViewInfo->UpdateUploadBuffer())
		ResourceManager::GetInstance().QueueDynamicBufferUpdate(m_directionalViewInfo.get());
}

void LightManager::UpdateNumDrawsInScene(unsigned int numDraws) {
	unsigned int newSize = ((numDraws + m_commandBufferIncrementSize) / m_commandBufferIncrementSize) * m_commandBufferIncrementSize;
	if (m_commandBufferSize != newSize) {
		m_commandBufferSize = newSize;
		for (auto& pair : m_lightDrawSetBufferMap) {
			markForDelete(pair.second.GetResource()); // Delay deletion until after the current frame
			auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer<IndirectCommand>(m_commandBufferSize, ResourceState::UNORDERED_ACCESS, false, true);
			pair.second.SetResource(resource.dataBuffer);
		}
	}
}