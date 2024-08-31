#include "PixelBuffer.h"
#include <string>
#include <stdexcept>

#include "DeviceManager.h"
#include "Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "ResourceManager.h"

PixelBuffer::PixelBuffer(const stbi_uc* image, int width, int height, bool sRGB) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    handle = resourceManager.CreateTexture(image, width, height, sRGB);
}

UINT PixelBuffer::GetDescriptorIndex() const {
    return handle.index;
}