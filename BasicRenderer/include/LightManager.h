#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include "buffers.h"
#include "ResourceHandles.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "SettingsManager.h"
#include "ResourceManager.h"

class Camera;
class Light;
class ShadowMaps;
class SceneNode;

class LightManager : public ISceneNodeObserver<Light>, public ISceneNodeObserver<SceneNode> {
public:
    LightManager();
    void Initialize();
    void AddLight(Light* lightNode, bool shadowCasting = false, Camera* currentCamera = nullptr);
    void RemoveLight(Light* light);
    unsigned int GetLightBufferDescriptorIndex();
    unsigned int GetPointCubemapMatricesDescriptorIndex();
    unsigned int GetSpotMatricesDescriptorIndex();
    unsigned int GetDirectionalCascadeMatricesDescriptorIndex();
    unsigned int GetNumLights();
    void SetCurrentCamera(Camera* camera);
    void OnNodeUpdated(SceneNode* camera) override;
    void OnNodeUpdated(Light* light) override;

    void UpdateBuffers() {
        if (m_lightBufferHandle.buffer->UpdateUploadBuffer())
		    ResourceManager::GetInstance().QueueDynamicBufferUpdate(m_lightBufferHandle);
		if (m_spotViewInfoHandle.buffer->UpdateUploadBuffer())
            ResourceManager::GetInstance().QueueDynamicBufferUpdate(m_spotViewInfoHandle);
		if (m_pointViewInfoHandle.buffer->UpdateUploadBuffer())
            ResourceManager::GetInstance().QueueDynamicBufferUpdate(m_pointViewInfoHandle);
		if (m_directionalViewInfoHandle.buffer->UpdateUploadBuffer())
            ResourceManager::GetInstance().QueueDynamicBufferUpdate(m_directionalViewInfoHandle);
    }

private:
    DynamicBufferHandle<LightInfo> m_lightBufferHandle;
    DynamicBufferHandle<DirectX::XMMATRIX> m_spotViewInfoHandle;
    DynamicBufferHandle<DirectX::XMMATRIX> m_pointViewInfoHandle;
    DynamicBufferHandle<DirectX::XMMATRIX> m_directionalViewInfoHandle;
    //std::unordered_map<int, unsigned int> lightIndexMap; // Maps localID to buffer index
    std::vector<Light*> m_lights; // Active light IDs
    std::vector<Light*> m_spotLights;
	std::vector<Light*> m_pointLights;
	std::vector<Light*> m_directionalLights;

    // Settings funcs
	std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<std::vector<float>()> getDirectionalLightCascadeSplits;
    std::function<uint16_t()> getShadowResolution;
    std::function<ShadowMaps*()> getCurrentShadowMapResourceGroup;

    Camera* m_currentCamera = nullptr;

    unsigned int CreateLightInfo(Light* node);
    unsigned int CreateLightViewInfo(Light* node, Camera* camera = nullptr);
    void UpdateLightViewInfo(Light* node);
	void RemoveLightViewInfo(Light* node);
};
