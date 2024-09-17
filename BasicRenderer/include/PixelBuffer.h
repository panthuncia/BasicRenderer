#pragma once
#include "wrl.h"
#include "stb/stb_image.h"
#include "d3d12.h"
#include "DirectX/d3dx12.h"

#include "ResourceHandles.h"
using Microsoft::WRL::ComPtr;

class PixelBuffer {
public:
    static std::shared_ptr<PixelBuffer> CreateFromImage(const stbi_uc* image, int width, int height, int channels, bool sRGB) {
		return std::shared_ptr<PixelBuffer>(new PixelBuffer(image, width, height, channels, sRGB));
    }
    static std::shared_ptr<PixelBuffer> CreateSingleTexture(int width, int height, int channels, bool isCubemap = false, bool RTV = false, bool DSV = false, bool UAV = false) {
    
    }
	static std::shared_ptr<PixelBuffer> CreateTextureArray(int width, int height, int channels, int numTextures, bool RTV = false, bool DSV = false, bool UAV = false) {

	}
    UINT GetSRVDescriptorIndex() const;
private:
    PixelBuffer(const stbi_uc* image, int width, int height, int channels, bool sRGB);
    PixelBuffer(int width, int height, int channels, bool isCubemap = false, bool RTV = false, bool DSV = false, bool UAV = false);
    PixelBuffer(int width, int height, int channels, int numTextures, bool RTV = false, bool DSV = false, bool UAV = false);

    TextureHandle<PixelBuffer> handle;

    friend class Texture;
};