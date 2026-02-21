#pragma once

#include <memory>
#include <string>
#include <rhi.h>

#include "Resources/Resource.h"

class GpuBufferBacking;

class ExternalBackingResource final : public Resource {
public:
    static std::shared_ptr<ExternalBackingResource> CreateShared(std::unique_ptr<GpuBufferBacking> backing);

    rhi::Resource GetAPIResource() override;

    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec r,
        rhi::ResourceAccessType prevA, rhi::ResourceAccessType newA,
        rhi::ResourceLayout prevL, rhi::ResourceLayout newL,
        rhi::ResourceSyncState prevS, rhi::ResourceSyncState newS) override;

    SymbolicTracker* GetStateTracker() override;

    bool TryGetBufferByteSize(uint64_t& outByteSize) const override;

    ~ExternalBackingResource() override;

private:
    struct Impl;

    ExternalBackingResource(std::unique_ptr<GpuBufferBacking> backing);

    std::unique_ptr<Impl> m_impl;
};
