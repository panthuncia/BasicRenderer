#pragma once

#include <memory>
#include <optional>

#include <rhi_helpers.h>

#include "Resources/AliasingPlacement.h"
#include "Resources/GloballyIndexedResource.h"
#include "Resources/TextureDescription.h"
#include "Resources/MemoryStatisticsComponents.h"
#include "Interfaces/IHasMemoryMetadata.h"

class GpuTextureBacking;

class PixelBuffer : public GloballyIndexedResource, public IHasMemoryMetadata {
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

    rhi::Resource GetAPIResource() override;
    rhi::BarrierBatch GetEnhancedBarrierGroup(
        RangeSpec range, 
        rhi::ResourceAccessType prevAccessType, 
        rhi::ResourceAccessType newAccessType, 
        rhi::ResourceLayout prevLayout, 
        rhi::ResourceLayout newLayout, 
        rhi::ResourceSyncState prevSyncState, 
        rhi::ResourceSyncState newSyncState)
	;

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
    void OnSetName() override;

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) const;

    SymbolicTracker* GetStateTracker() override;

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

    void Materialize(const MaterializeOptions* options = nullptr);

    void Dematerialize();

    void EnsureVirtualDescriptorSlotsAllocated();

    ~PixelBuffer() override;

private:
    PixelBuffer(const TextureDescription& desc, bool materialize);

    void EnsureMaterialized(const char* operation) const;

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) override;

    std::unique_ptr<GpuTextureBacking> m_backing;
	TextureDescription m_desc;
    uint64_t m_backingGeneration = 0;
    bool m_allowIdleDematerialization = false;
    uint32_t m_idleDematerializationThreshold = 1;
    uint32_t m_internalWidth = 0; // Internal width, used for padding textures to power of two
    uint32_t m_internalHeight = 0; // Internal height, used for padding textures to power of two
    rhi::ClearValue m_clearValue;
};
