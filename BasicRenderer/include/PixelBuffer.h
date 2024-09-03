#pragma once
#include "wrl.h"
#include "stb/stb_image.h"
#include "d3d12.h"
#include "DirectX/d3dx12.h"

#include "ResourceHandles.h"
using Microsoft::WRL::ComPtr;

class PixelBuffer {
public:
    PixelBuffer(const stbi_uc* image, int width, int height, int channels, bool sRGB);
    UINT GetDescriptorIndex() const;
private:
    TextureHandle<PixelBuffer> handle;
};