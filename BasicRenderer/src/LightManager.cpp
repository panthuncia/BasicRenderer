#include "LightManager.h"

#include "ResourceHandles.h"
#include "ResourceManager.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "Utilities.h"
#include "SettingsManager.h"
LightManager::LightManager() {
    auto& resourceManager = ResourceManager::GetInstance();
    m_lightBufferHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<LightInfo>(1);
    m_spotViewInfoHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(1);
    m_pointViewInfoHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(1);
    m_directionalViewInfoHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(1);
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
		CreateLightViewInfo(lightNode, currentCamera);
		auto shadowMap = getCurrentShadowMapResourceGroup();
		if (shadowMap != nullptr) {
			shadowMap->AddMap(lightNode, getShadowResolution());
		}
	}
    lightNode->AddLightObserver(this);
}

unsigned int LightManager::CreateLightInfo(Light* node) {
    m_lightBufferHandle.buffer.Add(node->GetLightInfo());
    m_lightBufferHandle.buffer.UpdateBuffer();
    return m_lightBufferHandle.buffer.Size() - 1; // Return new light's index
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

    m_lightBufferHandle.buffer.RemoveAt(index);
    m_lightBufferHandle.buffer.UpdateBuffer();
	light->RemoveLightObserver(this);
}

unsigned int LightManager::GetLightBufferDescriptorIndex() {
    return m_lightBufferHandle.index;
}

unsigned int LightManager::GetPointCubemapMatricesDescriptorIndex() {
	return m_pointViewInfoHandle.index;
}

unsigned int LightManager::GetSpotMatricesDescriptorIndex() {
	return m_spotViewInfoHandle.index;
}

unsigned int LightManager::GetDirectionalCascadeMatricesDescriptorIndex() {
	return m_directionalViewInfoHandle.index;
}

unsigned int LightManager::GetNumLights() {
    return m_lights.size();
}

unsigned int LightManager::CreateLightViewInfo(Light* node, Camera* camera) {
    auto projectionMatrix = node->GetLightProjectionMatrix();
	switch (node->GetLightType()) {
	case LightType::Point: {
		auto cubeViewIndex = m_pointViewInfoHandle.buffer.Size() / 6;
		node->SetLightViewInfoIndex(cubeViewIndex);
		auto cubemapMatrices = node->GetCubemapViewMatrices();
		for (int i = 0; i < 6; i++) {
			m_pointViewInfoHandle.buffer.Add(XMMatrixMultiply(cubemapMatrices[i], projectionMatrix));
		}
		break;
	}
	case LightType::Spot: {
		node->SetLightViewInfoIndex(m_spotViewInfoHandle.buffer.Size());
		m_spotViewInfoHandle.buffer.Add(XMMatrixMultiply(node->GetLightViewMatrix(), projectionMatrix));
		break;
	}
	case LightType::Directional: {
		if (camera == nullptr) {
			spdlog::warn("Camera must be provided for directional light shadow mapping");
			return -1;
		}
		auto numCascades = getNumDirectionalLightCascades();
		node->SetLightViewInfoIndex(m_directionalViewInfoHandle.buffer.Size() / numCascades);
		auto cascades = setupCascades(numCascades, *node, *camera, getDirectionalLightCascadeSplits());
		for (int i = 0; i < numCascades; i++) {
			m_directionalViewInfoHandle.buffer.Add(XMMatrixMultiply(cascades[i].orthoMatrix, cascades[i].viewMatrix));
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
		auto cubemapMatrices = light->GetCubemapViewMatrices();
		for (int i = 0; i < 6; i++) {
			m_pointViewInfoHandle.buffer.UpdateAt(light->GetCurrentviewInfoIndex()*6+i,XMMatrixMultiply(cubemapMatrices[i], projectionMatrix));
		}
		break;
	}
	case LightType::Spot: {
		light->SetLightViewInfoIndex(m_spotViewInfoHandle.buffer.Size());
		m_spotViewInfoHandle.buffer.UpdateAt(light->GetCurrentviewInfoIndex(), XMMatrixMultiply(light->GetLightViewMatrix(), projectionMatrix));
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
			m_directionalViewInfoHandle.buffer.UpdateAt(light->GetCurrentviewInfoIndex()*numCascades+i, XMMatrixMultiply(cascades[i].viewMatrix, cascades[i].orthoMatrix));
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
			m_pointViewInfoHandle.buffer.RemoveAt(viewInfoIndex);
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
		m_spotViewInfoHandle.buffer.RemoveAt(node->GetCurrentviewInfoIndex());
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
			m_directionalViewInfoHandle.buffer.RemoveAt(viewInfoIndex);
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