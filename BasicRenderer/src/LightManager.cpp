#include "LightManager.h"

#include "ResourceHandles.h"
#include "ResourceManager.h"

LightManager::LightManager() {
    auto& resourceManager = ResourceManager::GetInstance();
    lightBufferHandle = resourceManager.CreateDynamicStructuredBuffer<LightInfo>(1);
}

void LightManager::AddLight(Light& lightNode) {
    unsigned int index = CreateLightInfo(lightNode);
    lightIDs.push_back(lightNode.localID);
    lightIndexMap[lightNode.localID] = index;
}

unsigned int LightManager::CreateLightInfo(Light& node) {
    
    lightBufferHandle.buffer.Add(node.GetLightInfo());
    lightBufferHandle.buffer.UpdateBuffer();
    return lightBufferHandle.buffer.Size() - 1; // Return new light's index
}

void LightManager::RemoveLightByID(int localID) {
    auto it = lightIndexMap.find(localID);
    if (it != lightIndexMap.end()) {
        unsigned int bufferIndex = it->second;

        // Use binary search to find the position of localID in the light ID vector
        auto vecIt = std::lower_bound(lightIDs.begin(), lightIDs.end(), localID);

        // Check if the found position actually contains the localID
        if (vecIt != lightIDs.end() && *vecIt == localID) {
            lightIDs.erase(vecIt); // Remove the ID from the vector
        }
        else {
            spdlog::warn("Light requested for removal does not exist! Something's wrong.");
        }

        // Remove the light from the structured buffer
        lightBufferHandle.buffer.RemoveAt(bufferIndex);
        lightBufferHandle.buffer.UpdateBuffer();

        // Erase the ID entry from the map
        lightIndexMap.erase(it);
    }
}

unsigned int LightManager::GetLightBufferDescriptorIndex() {
    return lightBufferHandle.index;
}

unsigned int LightManager::GetNumLights() {
    return lightIDs.size();
}