#pragma once

#include <stdexcept>

#include "ThirdParty/stb/stb_image.h"

#include "Resources/GloballyIndexedResource.h"
#include "Resources/TextureDescription.h"
#include "Resources/GPUBacking/GPUTextureBacking.h"
#include "Managers/Singletons/ResourceManager.h"

class PixelBuffer : public GloballyIndexedResource {
public:
    static std::shared_ptr<PixelBuffer> CreateShared(const TextureDescription& desc)
    {
        auto pb = std::shared_ptr<PixelBuffer>(new PixelBuffer(desc, true));
        return pb;
    }

    static std::shared_ptr<PixelBuffer> CreateSharedUnmaterialized(const TextureDescription& desc)
    {
        auto pb = std::shared_ptr<PixelBuffer>(new PixelBuffer(desc, false));
        return pb;
    }

    rhi::Resource GetAPIResource() override {
        EnsureMaterialized("GetAPIResource");
        return m_backing->GetAPIResource();
    }
    rhi::BarrierBatch GetEnhancedBarrierGroup(
        RangeSpec range, 
        rhi::ResourceAccessType prevAccessType, 
        rhi::ResourceAccessType newAccessType, 
        rhi::ResourceLayout prevLayout, 
        rhi::ResourceLayout newLayout, 
        rhi::ResourceSyncState prevSyncState, 
        rhi::ResourceSyncState newSyncState)
    {
	    EnsureMaterialized("GetEnhancedBarrierGroup");
	    return m_backing->GetEnhancedBarrierGroup(
            range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState
        );
    }

	rhi::Format GetFormat() const {
        return m_desc.format;
    }
	bool IsBlockCompressed() const { return rhi::helpers::IsBlockCompressed(GetFormat()); }
    const rhi::ClearValue& GetClearColor() const {
        return m_clearValue;
    }
    unsigned int GetInternalWidth() const {
        return m_internalWidth;
    }
    unsigned int GetInternalHeight() const {
        return m_internalHeight;
    }
    unsigned int GetWidth() const {
        return m_desc.imageDimensions[0].width;
    }
    unsigned int GetHeight() const {
        return m_desc.imageDimensions[0].height;
    }
    unsigned int GetChannelCount()const {
	    return m_desc.channels;
    }
    bool IsCubemap() const {
        return m_desc.isCubemap;
	}
    void OnSetName() override {
		if (m_backing) {
            m_backing->SetName(name.c_str());
        }
	}

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) const {
        if (!m_backing) {
            return;
        }
        m_backing->ApplyMetadataComponentBundle(bundle);
    }

    SymbolicTracker* GetStateTracker() override {
        EnsureMaterialized("GetStateTracker");
        return m_backing->GetStateTracker();
    }

    bool IsMaterialized() const {
        return m_backing != nullptr;
    }

    void Materialize() {
        if (m_backing) {
            return;
        }

        auto newDesc = m_desc;
        // Pad each mip to the nearest power of 2, if requested
        for (auto& dim : m_desc.imageDimensions) {
            dim.width = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(dim.width)))));
            dim.height = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(dim.height)))));
            // TODO: Row and slice pitch?
        }

        m_backing = GpuTextureBacking::CreateUnique(newDesc, GetGlobalResourceID(), name.empty() ? nullptr : name.c_str());

		m_mipLevels = m_backing->GetMipLevels();
		m_arraySize = m_backing->GetArraySize();

        auto& rm = ResourceManager::GetInstance();
        ResourceManager::ViewRequirements views;
        ResourceManager::ViewRequirements::TextureViews texViews;
        texViews.mipLevels = m_mipLevels;
        texViews.isCubemap = m_desc.isCubemap;
        texViews.isArray = m_desc.isArray;
        texViews.arraySize = m_desc.arraySize;
        texViews.totalArraySlices = m_arraySize;

        texViews.baseFormat = m_desc.format;
        texViews.srvFormat = m_desc.srvFormat;
        texViews.uavFormat = m_desc.uavFormat;
        texViews.rtvFormat = m_desc.rtvFormat;
        texViews.dsvFormat = m_desc.dsvFormat;

        texViews.createSRV = true;
        texViews.createUAV = m_desc.hasUAV;
        texViews.createNonShaderVisibleUAV = m_desc.hasNonShaderVisibleUAV;
        texViews.createRTV = m_desc.hasRTV;
        texViews.createDSV = m_desc.hasDSV;

        if (m_desc.hasUAV && rhi::helpers::IsSRGB(m_desc.format)) {
            if (texViews.srvFormat == rhi::Format::Unknown) {
                texViews.srvFormat = m_desc.format;
            }
            texViews.baseFormat = rhi::helpers::typlessFromSrgb(m_desc.format);
            texViews.uavFormat = rhi::helpers::stripSrgb(m_desc.format);
        }

        texViews.createCubemapAsArraySRV = m_desc.isCubemap;
        texViews.uavFirstMip = 0;

        views.views = texViews;
        auto res = m_backing->GetAPIResource();
        rm.AssignDescriptorSlots(*this, res, views);
    }

private:
    PixelBuffer(const TextureDescription& desc, bool materialize)
    {
		m_hasLayout = true; // This is a texture, so it has a layout by default
        m_desc = desc;
        if (materialize) {
            Materialize();
        }
        if (desc.padInternalResolution) {
            m_internalWidth = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].width)))));
            m_internalHeight = std::max(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].height)))));
        }
        else {
            m_internalHeight = desc.imageDimensions[0].height;
            m_internalWidth = desc.imageDimensions[0].width;
        }

        m_clearValue.type = rhi::ClearValueType::Color;
        m_clearValue.format = desc.format;
        m_clearValue.depthStencil.depth = desc.depthClearValue;
        if (desc.hasDSV) {
            m_clearValue.type = rhi::ClearValueType::DepthStencil; // TODO: Will we ever need both on one texture?
        }
        else {
            for (int i = 0; i < 4; i++) {
                m_clearValue.rgba[i] = desc.clearColor[i];
            }
        }
	}

    void EnsureMaterialized(const char* operation) const {
        if (m_backing) {
            return;
        }
        throw std::runtime_error(std::string("PixelBuffer '") + name + "' is unmaterialized during " + operation);
    }

    std::unique_ptr<GpuTextureBacking> m_backing;
	TextureDescription m_desc;
    uint32_t m_internalWidth = 0; // Internal width, used for padding textures to power of two
    uint32_t m_internalHeight = 0; // Internal height, used for padding textures to power of two
    rhi::ClearValue m_clearValue;
};
