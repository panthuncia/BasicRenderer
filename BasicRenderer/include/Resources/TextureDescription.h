#pragma once
#include <directx/d3d12.h>
#include "Resources/ResourceStates.h"

struct ImageDimensions {
    int width = 0;
    int height = 0;
    int rowPitch = 0;
    int slicePitch = 0;
};

struct TextureDescription {
	std::vector<ImageDimensions> imageDimensions;
    int channels = 0; // Number of channels in the data (e.g., 3 for RGB, 4 for RGBA)
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool isCubemap = false;
    bool isArray = false;
    uint32_t arraySize = 1;
    bool hasRTV = false;
	DXGI_FORMAT rtvFormat = DXGI_FORMAT_UNKNOWN;
    bool hasDSV = false;
	DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
    bool hasUAV = false;
	DXGI_FORMAT uavFormat = DXGI_FORMAT_UNKNOWN;
	bool hasSRV = false;
	DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
	bool hasNonShaderVisibleUAV = false;
    bool generateMipMaps = false;
	bool allowAlias = false;
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // default RGBA clear color
	float depthClearValue = 1.0f; // default depth clear value
    bool padInternalResolution = false; // If true, the texture will be padded to the next power of two resolution
};