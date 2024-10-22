#pragma once
#include <directx/d3d12.h>
#include "ResourceStates.h"
struct TextureDescription {
    int width = 0;
    int height = 0;
    int channels = 0; // Number of channels in the data (e.g., 3 for RGB, 4 for RGBA)
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool isCubemap = false;
    bool isArray = false;
    uint32_t arraySize = 1;
    bool hasRTV = false;
    bool hasDSV = false;
    bool hasUAV = false;
    bool generateMipMaps = false;
    ResourceState initialState = ResourceState::UNKNOWN;
};