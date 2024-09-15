#include "LightManager.h"

#include "ResourceHandles.h"
#include "ResourceManager.h"
#include "Interfaces/ISceneNodeObserver.h"

LightManager::LightManager() {
    auto& resourceManager = ResourceManager::GetInstance();
    m_lightBufferHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<LightInfo>(1);
    m_spotViewInfoHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(1);
    m_pointViewInfoHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(1);
    m_directionalViewInfoHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<DirectX::XMMATRIX>(1);
}

void LightManager::AddLight(Light* lightNode) {
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
	CreateLightViewInfo(lightNode);
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
    
    for (int i = index+1; i < m_lights.size(); i++) {
        m_lights[i]->DecrementLightBufferIndex();
    }

    m_lights.erase(m_lights.begin() + index);

    m_lightBufferHandle.buffer.RemoveAt(index);
    m_lightBufferHandle.buffer.UpdateBuffer();
}

unsigned int LightManager::GetLightBufferDescriptorIndex() {
    return m_lightBufferHandle.index;
}

unsigned int LightManager::GetNumLights() {
    return m_lights.size();
}

unsigned int LightManager::CreateLightViewInfo(Light* node) {
	switch (node->GetLightType()) {
	case LightType::Point:
		auto cubeViewIndex = m_pointViewInfoHandle.buffer.Size()/6;
		node->SetLightViewInfoIndex(cubeViewIndex);
		auto cubemapMatrices = node->GetCubemapViewMatrices();
        for (int i = 0; i < 6; i++) {
            m_pointViewInfoHandle.buffer.Add(cubemapMatrices[i]);
        }
		break;
	case LightType::Spot:
		node->SetLightViewInfoIndex(m_spotViewInfoHandle.buffer.Size());
		m_spotViewInfoHandle.buffer.Add(node->GetLightViewMatrix());
		break;
	

	default:
		spdlog::warn("Light type not recognized");
		return -1;
	}
}