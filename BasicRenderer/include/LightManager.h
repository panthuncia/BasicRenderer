#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include "Light.h"
#include "buffers.h"
#include "ResourceHandles.h"
#include "Interfaces/ISceneNodeObserver.h"

class LightManager : public ISceneNodeObserver<Light> {
public:
    LightManager();
    void Initialize();
    void AddLight(Light* lightNode);
    void RemoveLight(Light* light);
    unsigned int GetLightBufferDescriptorIndex();
    unsigned int GetNumLights();
    void OnNodeUpdated(Light* light) override {
        lightBufferHandle.buffer.UpdateAt(light->GetCurrentLightBufferIndex(), light->GetLightInfo());
    }

    void UpdateGPU() {
        lightBufferHandle.buffer.UpdateBuffer();
    }

private:
    DynamicBufferHandle<LightInfo> lightBufferHandle;
    DynamicBufferHandle<DirectX::XMMATRIX> spotViewInfoHandle;
    DynamicBufferHandle<DirectX::XMMATRIX> pointViewInfoHandle;
    DynamicBufferHandle<DirectX::XMMATRIX> directionalViewInfoHandle;
    //std::unordered_map<int, unsigned int> lightIndexMap; // Maps localID to buffer index
    std::vector<Light*> lights; // Active light IDs

    unsigned int CreateLightInfo(Light* node);
    unsigned int CreateLightViewInfo(Light* node);
};
