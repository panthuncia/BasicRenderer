#pragma once

#include <rhi.h>

struct ImageDimensions {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t rowPitch = 0;
    uint64_t slicePitch = 0;
};

struct TextureDescription {
    // If you only have base level dimensions/pitches, you may provide a single entry
    // If you provide per-subresource dimensions/pitches, order is [slice0 mip0..mipN-1, slice1 ...]
	std::vector<ImageDimensions> imageDimensions;

    unsigned short channels = 0; // Number of channels in the data (e.g., 3 for RGB, 4 for RGBA)
    rhi::Format format = rhi::Format::Unknown;

    bool isCubemap = false;
    bool isArray = false;
	uint32_t arraySize = 1; // Number of slices, or number of cubemaps if isCubemap is true

    bool hasRTV = false;
    rhi::Format rtvFormat = rhi::Format::Unknown;
    bool hasDSV = false;
    rhi::Format dsvFormat = rhi::Format::Unknown;
    bool hasUAV = false;
    rhi::Format uavFormat = rhi::Format::Unknown;
	bool hasSRV = false;
    rhi::Format srvFormat = rhi::Format::Unknown;
	bool hasNonShaderVisibleUAV = false;

    bool generateMipMaps = false;
	bool allowAlias = false;

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // default RGBA clear color
	float depthClearValue = 1.0f; // default depth clear value

    bool padInternalResolution = false; // If true, the texture will be padded to the next power of two resolution
};