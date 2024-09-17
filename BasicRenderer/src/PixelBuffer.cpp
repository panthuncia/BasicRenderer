#include "PixelBuffer.h"
#include <string>
#include <stdexcept>

#include "DeviceManager.h"
#include "Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "ResourceManager.h"

PixelBuffer::PixelBuffer(const stbi_uc* image, int width, int height, int channels, bool sRGB) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    handle = resourceManager.CreateTextureFromImage(image, width, height, channels, sRGB);
}

UINT PixelBuffer::GetSRVDescriptorIndex() const {
    return handle.SRVInfo.index;
}