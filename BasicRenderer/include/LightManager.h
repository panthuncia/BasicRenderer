#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include "Light.h"
#include "buffers.h"
#include "ResourceHandles.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "Camera.h"

class LightManager : public ISceneNodeObserver<Light> {
public:
    LightManager();
    void Initialize();
    void AddLight(Light* lightNode, bool shadowCasting = false, Camera* currentCamera = nullptr);
    void RemoveLight(Light* light);
    unsigned int GetLightBufferDescriptorIndex();
    unsigned int GetNumLights();
    void OnNodeUpdated(Light* light) override {
        m_lightBufferHandle.buffer.UpdateAt(light->GetCurrentLightBufferIndex(), light->GetLightInfo());
    }

    void UpdateGPU() {
        m_lightBufferHandle.buffer.UpdateBuffer();
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

    unsigned int CreateLightInfo(Light* node);
    unsigned int CreateLightViewInfo(Light* node, Camera* camera = nullptr);
	void RemoveLightViewInfo(Light* node);

};
