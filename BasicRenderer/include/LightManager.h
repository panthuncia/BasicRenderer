#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include "Light.h"
#include "buffers.h"
#include "ResourceHandles.h"

class LightManager {
public:
    LightManager();
    void Initialize();
    void AddLight(Light& lightNode);
    void RemoveLightByID(int localID);
    unsigned int GetLightBufferDescriptorIndex();
    unsigned int GetNumLights();
private:
    DynamicBufferHandle<LightInfo> lightBufferHandle;
    std::unordered_map<int, unsigned int> lightIndexMap; // Maps localID to buffer index
    std::vector<unsigned int> lightIDs; // Active light IDs

    unsigned int CreateLightInfo(Light& node);
};
