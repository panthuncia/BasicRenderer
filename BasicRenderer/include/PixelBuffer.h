#pragma once
#include "wrl.h"
#include "stb/stb_image.h"
#include "d3d12.h"
#include "DirectX/d3dx12.h"

#include "ResourceStates.h"
#include "ResourceHandles.h"
#include "GloballyIndexedResource.h"

using Microsoft::WRL::ComPtr;

class PixelBuffer : public GloballyIndexedResource {
public:
    static std::shared_ptr<PixelBuffer> CreateFromImage(const stbi_uc* image, int width, int height, int channels, bool sRGB) {
		return std::shared_ptr<PixelBuffer>(new PixelBuffer(image, width, height, channels, sRGB));
    }
    static std::shared_ptr<PixelBuffer> CreateSingleTexture(int width, int height, int channels, bool isCubemap = false, bool RTV = false, bool DSV = false, bool UAV = false) {
		return std::shared_ptr<PixelBuffer>(new PixelBuffer(width, height, channels, isCubemap, RTV, DSV, UAV));
    }
	static std::shared_ptr<PixelBuffer> CreateTextureArray(int width, int height, int channels, int numTextures, bool cubemap = false, bool RTV = false, bool DSV = false, bool UAV = false) {
		return std::shared_ptr<PixelBuffer>(new PixelBuffer(width, height, channels, numTextures, cubemap, RTV, DSV, UAV));
	}
    UINT GetSRVDescriptorIndex() const;
	TextureHandle<PixelBuffer>& GetHandle() { return handle; }
    void Transition(const RenderContext& context, ResourceState fromState, ResourceState toState);
    virtual void SetName(const std::wstring& name) { this->name = name; handle.texture->SetName(name.c_str()); }

private:
    PixelBuffer(const stbi_uc* image, int width, int height, int channels, bool sRGB);
    PixelBuffer(int width, int height, int channels, bool isCubemap = false, bool RTV = false, bool DSV = false, bool UAV = false);
    PixelBuffer(int width, int height, int channels, int numTextures, bool isCubemap, bool RTV = false, bool DSV = false, bool UAV = false);

    TextureHandle<PixelBuffer> handle;
};