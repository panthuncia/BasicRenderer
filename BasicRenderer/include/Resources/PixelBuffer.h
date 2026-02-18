#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ThirdParty/stb/stb_image.h"

#include "Resources/GloballyIndexedResource.h"
#include "Resources/TextureDescription.h"
#include "Resources/GPUBacking/GPUTextureBacking.h"
#include "Resources/MemoryStatisticsComponents.h"
#include "Managers/Singletons/ResourceManager.h"

class PixelBuffer : public GloballyIndexedResource {
public:
    struct MaterializeOptions {
        std::optional<TextureAliasPlacement> aliasPlacement;
    };

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

    uint64_t GetBackingGeneration() const {
        return m_backingGeneration;
    }

    void SetAliasingPool(uint64_t poolID) {
        m_desc.aliasingPoolID = poolID;
        m_desc.allowAlias = true;
    }

    void ClearAliasingPoolHint() {
        m_desc.aliasingPoolID.reset();
    }

    std::optional<uint64_t> GetAliasingPoolHint() const {
        return m_desc.aliasingPoolID;
    }

	const TextureDescription& GetDescription() const {
		return m_desc;
	}

    void EnableIdleDematerialization(uint32_t idleFrameThreshold = 1) {
        m_allowIdleDematerialization = true;
        m_idleDematerializationThreshold = (std::max)(1u, idleFrameThreshold);
    }

    void DisableIdleDematerialization() {
        m_allowIdleDematerialization = false;
    }

    bool IsIdleDematerializationEnabled() const {
        return m_allowIdleDematerialization;
    }

    uint32_t GetIdleDematerializationThreshold() const {
        return m_idleDematerializationThreshold;
    }

    void Materialize(const MaterializeOptions* options = nullptr) {
        if (m_backing) {
            return;
        }

        EnsureVirtualDescriptorSlotsAllocated();

        auto newDesc = m_desc;
        if (m_desc.padInternalResolution) {
            // Pad each mip to the nearest power of 2, if requested
            for (auto& dim : m_desc.imageDimensions) {
                dim.width = (std::max)(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(dim.width)))));
                dim.height = (std::max)(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(dim.height)))));
                // TODO: Row and slice pitch?
            }
        }

        if (options && options->aliasPlacement.has_value()) {
            m_backing = GpuTextureBacking::CreateUnique(newDesc, GetGlobalResourceID(), options->aliasPlacement.value(), name.empty() ? nullptr : name.c_str());
        }
        else {
        	m_backing = GpuTextureBacking::CreateUnique(newDesc, GetGlobalResourceID(), name.empty() ? nullptr : name.c_str());
        }

		m_mipLevels = m_backing->GetMipLevels();
		m_arraySize = m_backing->GetArraySize();

        auto& rm = ResourceManager::GetInstance();
        ResourceManager::ViewRequirements views;
        views.views = BuildTextureViewRequirements(m_desc, m_mipLevels, m_arraySize);
        auto res = m_backing->GetAPIResource();
        rm.UpdateDescriptorContents(*this, res, views);

        if (m_desc.aliasingPoolID.has_value()) {
            m_backing->ApplyMetadataComponentBundle(
                EntityComponentBundle().Set<MemoryStatisticsComponents::AliasingPool>({ m_desc.aliasingPoolID })
            );
        }

        ++m_backingGeneration;
    }

    void Dematerialize() {
        if (!m_backing) {
            return;
        }

        m_backing.reset();
        ++m_backingGeneration;
    }

    void EnsureVirtualDescriptorSlotsAllocated() {
        if (HasAnyDescriptorSlots()) {
            return;
        }

        auto& rm = ResourceManager::GetInstance();
        const uint16_t mipLevels = m_desc.generateMipMaps
            ? CalculateMipLevels(m_desc.imageDimensions[0].width, m_desc.imageDimensions[0].height)
            : 1;
        const uint32_t arraySize = m_desc.isCubemap
            ? 6u * m_desc.arraySize
            : (m_desc.isArray ? m_desc.arraySize : 1u);

        ResourceManager::ViewRequirements views;
        views.views = BuildTextureViewRequirements(m_desc, mipLevels, arraySize);
        rm.ReserveDescriptorSlots(*this, views);
    }

private:
    static ResourceManager::ViewRequirements::TextureViews BuildTextureViewRequirements(
        const TextureDescription& desc,
        uint32_t mipLevels,
        uint32_t totalArraySlices)
    {
        ResourceManager::ViewRequirements::TextureViews texViews;
        texViews.mipLevels = mipLevels;
        texViews.isCubemap = desc.isCubemap;
        texViews.isArray = desc.isArray;
        texViews.arraySize = desc.arraySize;
        texViews.totalArraySlices = totalArraySlices;

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

        if (desc.hasUAV && rhi::helpers::IsSRGB(desc.format)) {
            if (texViews.srvFormat == rhi::Format::Unknown) {
                texViews.srvFormat = desc.format;
            }
            texViews.baseFormat = rhi::helpers::typlessFromSrgb(desc.format);
            texViews.uavFormat = rhi::helpers::stripSrgb(desc.format);
        }

        texViews.createCubemapAsArraySRV = desc.isCubemap;
        texViews.uavFirstMip = 0;
        return texViews;
    }

    PixelBuffer(const TextureDescription& desc, bool materialize)
    {
		m_hasLayout = true; // This is a texture, so it has a layout by default
        m_desc = desc;
        if (materialize) {
            Materialize();
        }
        if (desc.padInternalResolution) {
            m_internalWidth = (std::max)(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].width)))));
            m_internalHeight = (std::max)(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].height)))));
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
    uint64_t m_backingGeneration = 0;
    bool m_allowIdleDematerialization = false;
    uint32_t m_idleDematerializationThreshold = 1;
    uint32_t m_internalWidth = 0; // Internal width, used for padding textures to power of two
    uint32_t m_internalHeight = 0; // Internal height, used for padding textures to power of two
    rhi::ClearValue m_clearValue;
};
