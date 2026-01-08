#pragma once

#include "ThirdParty/stb/stb_image.h"

#include "Resources/GloballyIndexedResource.h"
#include "Resources/TextureDescription.h"
#include "Resources/GPUBacking/GPUTextureBacking.h"
#include "Managers/Singletons/ResourceManager.h"

class PixelBuffer : public GloballyIndexedResource {
public:
    static std::shared_ptr<PixelBuffer> CreateShared(const TextureDescription& desc)
    {
        auto pb = std::shared_ptr<PixelBuffer>(new PixelBuffer(desc));
        return pb;
    }

    rhi::Resource GetAPIResource() override { return m_backing->GetAPIResource(); }
    rhi::BarrierBatch GetEnhancedBarrierGroup(
        RangeSpec range, 
        rhi::ResourceAccessType prevAccessType, 
        rhi::ResourceAccessType newAccessType, 
        rhi::ResourceLayout prevLayout, 
        rhi::ResourceLayout newLayout, 
        rhi::ResourceSyncState prevSyncState, 
        rhi::ResourceSyncState newSyncState)
    {
	    return m_backing->GetEnhancedBarrierGroup(
            range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState
        );
    }

    rhi::Format GetFormat() const { return m_backing->GetFormat(); }
	bool IsBlockCompressed() const { return rhi::helpers::IsBlockCompressed(GetFormat()); }
    const rhi::ClearValue& GetClearColor() const {
        return m_backing->GetClearColor();
    }
    unsigned int GetInternalWidth() const {
        return m_backing->GetInternalWidth();
    }
    unsigned int GetInternalHeight() const {
        return m_backing->GetInternalHeight();
    }
    unsigned int GetWidth() const {
        return m_backing->GetWidth();
    }
    unsigned int GetHeight() const {
        return m_backing->GetHeight();
    }
    unsigned int GetChannelCount()const {
	    return m_desc.channels;
    }
    bool IsCubemap() const {
        return m_desc.isCubemap;
	}
    void OnSetName() override {
        m_backing->SetName(name.c_str());
	}

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) const {
        m_backing->ApplyMetadataComponentBundle(bundle);
    }

    SymbolicTracker* GetStateTracker() override {
        return m_backing->GetStateTracker();
    }

private:
    PixelBuffer(const TextureDescription& desc)
    {
        m_desc = desc;
        m_backing = GpuTextureBacking::CreateUnique(desc, GetGlobalResourceID(), nullptr);

    	m_mipLevels = m_backing->GetMipLevels();
		m_arraySize = m_backing->GetArraySize();
        // Create and assign descriptors through ResourceManager.
        {
			auto& rm = ResourceManager::GetInstance();
            ResourceManager::ViewRequirements views;
            ResourceManager::ViewRequirements::TextureViews texViews;
            texViews.mipLevels = m_mipLevels;
            texViews.isCubemap = desc.isCubemap;
            texViews.isArray = desc.isArray;
            texViews.arraySize = desc.arraySize;
            texViews.totalArraySlices = m_arraySize;

            texViews.baseFormat = desc.format;
            texViews.srvFormat = desc.srvFormat;
            texViews.uavFormat = desc.uavFormat;
            texViews.rtvFormat = desc.rtvFormat;
            texViews.dsvFormat = desc.dsvFormat;

            texViews.createSRV = true;
            texViews.createUAV = desc.hasUAV;
            texViews.createNonShaderVisibleUAV = desc.hasNonShaderVisibleUAV;
            texViews.createRTV = desc.hasRTV;
            texViews.createDSV = desc.hasDSV;

            // if cubemap, also create Texture2DArray SRV.
            texViews.createCubemapAsArraySRV = desc.isCubemap;

            texViews.uavFirstMip = 0;

            views.views = texViews;
			auto res = m_backing->GetAPIResource();
            rm.AssignDescriptorSlots(*this, res, views);
        }
	}
    std::unique_ptr<GpuTextureBacking> m_backing;
	TextureDescription m_desc;
};
