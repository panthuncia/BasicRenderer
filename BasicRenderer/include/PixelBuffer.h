#pragma once
#include <initguid.h> // Why is this needed? Without it I get a linker error for IID_ID3D12Device.
#include "wrl.h"
#include "ThirdParty/stb/stb_image.h"
#include "DirectX/d3dx12.h"
#include <d3d12.h>

#include "ResourceStates.h"
#include "ResourceHandles.h"
#include "GloballyIndexedResource.h"
#include "TextureDescription.h"
using Microsoft::WRL::ComPtr;

class PixelBuffer : public GloballyIndexedResource {
public:
    static std::shared_ptr<PixelBuffer> Create(const TextureDescription& desc, const std::vector<const stbi_uc*> initialData = {}) {
		return std::shared_ptr<PixelBuffer>(new PixelBuffer(desc, initialData));
    }
	unsigned int GetWidth() const { return m_width; }
	unsigned int GetHeight() const { return m_height; }
	unsigned int GetChannels() const { return m_channels; }
    ComPtr<ID3D12Resource> GetTexture() const {
		return handle.texture;
    }
    std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState fromState, ResourceState toState);
    BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState);

    virtual void SetName(const std::wstring& name) { this->name = name; handle.texture->SetName(name.c_str()); }

	ID3D12Resource* GetAPIResource() const override { return handle.texture.Get(); }

private:
    PixelBuffer(const stbi_uc* image, int width, int height, int channels, bool sRGB);
    PixelBuffer(const std::array<const stbi_uc*, 6>& images, int width, int height, int channels, bool sRGB);
    PixelBuffer(const TextureDescription& desc, const std::vector<const stbi_uc*> initialData = {});

    TextureHandle<PixelBuffer> handle;
    unsigned int m_width;
    unsigned int m_height;
	unsigned int m_channels;
    DXGI_FORMAT m_format;

	// Old barriers
	std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
	std::vector<D3D12_RESOURCE_BARRIER> m_emptyTransitions = {};

    // Enhanced barriers
    D3D12_TEXTURE_BARRIER m_textureBarrier;
    D3D12_BARRIER_GROUP m_barrierGroup = {};
	BarrierGroups m_barrierGroups;
};