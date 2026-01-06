#pragma once

#include <memory>
#include <string>
#include <rhi.h>

#include "Resources/Resource.h"
#include "Resources/GPUBacking/GpuBufferBacking.h"

// This resource type is a simple wrapper around an externally created GpuBufferBacking.
// Used by UploadManager when it needs to provide a temporary ID to a buffer it owns
class ExternalBackingResource final : public Resource {
public:
    static std::shared_ptr<ExternalBackingResource> CreateShared(std::unique_ptr<GpuBufferBacking> backing)
    {
        return std::shared_ptr<ExternalBackingResource>(
            new ExternalBackingResource(std::move(backing))
        );
    }

    rhi::Resource GetAPIResource() override { return m_backing->GetAPIResource(); }

    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec r,
        rhi::ResourceAccessType prevA, rhi::ResourceAccessType newA,
        rhi::ResourceLayout prevL, rhi::ResourceLayout newL,
        rhi::ResourceSyncState prevS, rhi::ResourceSyncState newS) override
    {
        return m_backing->GetEnhancedBarrierGroup(r, prevA, newA, prevL, newL, prevS, newS);
    }

    SymbolicTracker* GetStateTracker() override {
        return m_backing->GetStateTracker();
	}

protected:

private:

    ExternalBackingResource(std::unique_ptr<GpuBufferBacking> backing)
        : m_backing(std::move(backing))
    {
        m_hasLayout = false;
        m_mipLevels = 1;
        m_arraySize = 1;
    }

    std::unique_ptr<GpuBufferBacking> m_backing;
};