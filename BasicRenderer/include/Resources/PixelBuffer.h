#pragma once
#include <initguid.h> // Why is this needed? Without it I get a linker error for IID_ID3D12Device.
#include "wrl.h"
#include "ThirdParty/stb/stb_image.h"
#include "DirectX/d3dx12.h"
#include <d3d12.h>
#include <array>

#include "Resources/ResourceStates.h"
#include "Resources/ResourceHandles.h"
#include "Resources/GloballyIndexedResource.h"
#include "Resources/TextureDescription.h"
using Microsoft::WRL::ComPtr;

class PixelBuffer : public GloballyIndexedResource {
public:
    static std::shared_ptr<PixelBuffer> Create(const TextureDescription& desc, const std::vector<const stbi_uc*> initialData = {}, PixelBuffer* textureToAlias = nullptr) {
		return std::shared_ptr<PixelBuffer>(new PixelBuffer(desc, initialData, textureToAlias));
    }
    static std::shared_ptr<PixelBuffer> Create(const TextureDescription& desc, PixelBuffer* textureToAlias) {
        return std::shared_ptr<PixelBuffer>(new PixelBuffer(desc, {}, textureToAlias));
    }
	unsigned int GetWidth() const { return m_width; }
	unsigned int GetHeight() const { return m_height; }
	unsigned int GetChannels() const { return m_channels; }
    ComPtr<ID3D12Resource> GetTexture() const {
		return handle.texture;
    }
    BarrierGroups GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState);

    virtual void SetName(const std::wstring& name) { this->name = name; handle.texture->SetName(name.c_str()); }

	ID3D12Resource* GetAPIResource() const override { return handle.texture.Get(); }

	ID3D12Heap* GetPlacedResourceHeap() const {
		return handle.placedResourceHeap.Get();
	}

	const std::array<float, 4>& GetClearColor() const {
		return m_clearColor;
	}

private:
    PixelBuffer(const TextureDescription& desc, const std::vector<const stbi_uc*> initialData = {}, PixelBuffer* textureToAlias = nullptr);

    TextureHandle<PixelBuffer> handle;
    unsigned int m_width;
    unsigned int m_height;
	unsigned int m_channels;
    DXGI_FORMAT m_format;
	TextureDescription m_desc;
	std::array<float, 4> m_clearColor;

	// Old barriers
	std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
	std::vector<D3D12_RESOURCE_BARRIER> m_emptyTransitions = {};

    // Enhanced barriers
    //D3D12_TEXTURE_BARRIER m_textureBarrier;
    D3D12_BARRIER_GROUP m_barrierGroup = {};
	//BarrierGroups m_barrierGroups;
};