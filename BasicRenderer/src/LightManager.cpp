#include "LightManager.h"

#include "ResourceHandles.h"
#include "ResourceManager.h"
#include "Interfaces/ISceneNodeObserver.h"

LightManager::LightManager() {
    auto& resourceManager = ResourceManager::GetInstance();
    lightBufferHandle = resourceManager.CreateIndexedDynamicStructuredBuffer<LightInfo>(1);
}

void LightManager::AddLight(Light* lightNode) {
    if (lightNode->GetCurrentLightBufferIndex() != -1) {
        RemoveLight(lightNode);
    }
    unsigned int index = CreateLightInfo(lightNode);
    lights.push_back(lightNode);
    lightNode->SetLightBufferIndex(index);
    lightNode->AddLightObserver(this);
}

unsigned int LightManager::CreateLightInfo(Light* node) {
    lightBufferHandle.buffer.Add(node->GetLightInfo());
    lightBufferHandle.buffer.UpdateBuffer();
    return lightBufferHandle.buffer.Size() - 1; // Return new light's index
}

void LightManager::RemoveLight(Light* light) {
    int index = light->GetCurrentLightBufferIndex();
    if (lights[index] != light) {
        spdlog::warn("Light requested for removal does not belong to this scene");
    }
    
    for (int i = index+1; i < lights.size(); i++) {
        lights[i]->DecrementLightBufferIndex();
    }

    lights.erase(lights.begin() + index);

    lightBufferHandle.buffer.RemoveAt(index);
    lightBufferHandle.buffer.UpdateBuffer();
}

unsigned int LightManager::GetLightBufferDescriptorIndex() {
    return lightBufferHandle.index;
}

unsigned int LightManager::GetNumLights() {
    return lights.size();
}