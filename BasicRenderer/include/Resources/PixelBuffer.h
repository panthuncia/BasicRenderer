#pragma once
#include <initguid.h> // Why is this needed? Without it I get a linker error for IID_ID3D12Device.

#include <rhi.h>

#include "ThirdParty/stb/stb_image.h"
#include <array>

#include "Resources/ResourceStates.h"
#include "Resources/GloballyIndexedResource.h"
#include "Resources/TextureDescription.h"
using Microsoft::WRL::ComPtr;

class PixelBuffer : public GloballyIndexedResource {
public:
    static std::shared_ptr<PixelBuffer>
        Create(const TextureDescription& desc,
            const std::vector<const stbi_uc*>& initialData = {},
            PixelBuffer* aliasTarget = nullptr);

	unsigned int GetWidth() const { return m_width; }
	unsigned int GetHeight() const { return m_height; }
	unsigned int GetChannels() const { return m_channels; }
    rhi::Resource GetTexture() const {
		return m_texture.Get();
    }
    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState);

    virtual void SetName(const std::wstring& newName) { name = newName; m_texture->SetName(ws2s(newName).c_str()); }

	rhi::Resource GetAPIResource() const override { return m_texture.Get(); }

	rhi::HeapHandle GetPlacedResourceHeap() const {
		return m_placedResourceHeap;
	}

	const std::array<float, 4>& GetClearColor() const {
		return m_clearColor;
	}

	unsigned int GetInternalWidth() const {
		return m_internalWidth;
	}
	unsigned int GetInternalHeight() const {
		return m_internalHeight;
	}

private:
    PixelBuffer() = default;
    void initialize(const TextureDescription& desc,
        const std::vector<const stbi_uc*>& initialData,
        PixelBuffer* aliasTarget);

    unsigned int m_width;
    unsigned int m_height;
	unsigned int m_channels;
    rhi::ResourcePtr m_texture;
    rhi::Format m_format;
	TextureDescription m_desc;
	std::array<float, 4> m_clearColor;

    rhi::HeapHandle m_placedResourceHeap; // If this is a placed resource, this is the heap it was created in

    // Enhanced barriers
    //D3D12_TEXTURE_BARRIER m_textureBarrier;
    rhi::TextureBarrier m_barrierGroup = {};
	//BarrierGroups m_barrierGroups;

	unsigned int m_internalWidth = 0; // Internal width, used for padding textures to power of two
	unsigned int m_internalHeight = 0; // Internal height, used for padding textures to power of two
};