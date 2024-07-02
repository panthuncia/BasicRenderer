#pragma once
#include "wrl.h"
#include "stb/stb_image.h"
#include "d3d12.h"
using Microsoft::WRL::ComPtr;

class Texture {
public:
    ComPtr<ID3D12Resource> textureResource;
    // Other texture-related members...

    Texture(const stbi_uc* image, int width, int height, bool sRGB);
    // Additional constructors or methods...
};