#include "Resources/ExternalBackingResource.h"

#include "Resources/GPUBacking/GpuBufferBacking.h"

struct ExternalBackingResource::Impl {
    explicit Impl(std::unique_ptr<GpuBufferBacking> inBacking)
        : backing(std::move(inBacking)) {
    }

    std::unique_ptr<GpuBufferBacking> backing;
};

std::shared_ptr<ExternalBackingResource> ExternalBackingResource::CreateShared(std::unique_ptr<GpuBufferBacking> backing)
{
    return std::shared_ptr<ExternalBackingResource>(
        new ExternalBackingResource(std::move(backing))
    );
}

rhi::Resource ExternalBackingResource::GetAPIResource() {
    return m_impl->backing->GetAPIResource();
}

rhi::BarrierBatch ExternalBackingResource::GetEnhancedBarrierGroup(RangeSpec r,
    rhi::ResourceAccessType prevA, rhi::ResourceAccessType newA,
    rhi::ResourceLayout prevL, rhi::ResourceLayout newL,
    rhi::ResourceSyncState prevS, rhi::ResourceSyncState newS)
{
    return m_impl->backing->GetEnhancedBarrierGroup(r, prevA, newA, prevL, newL, prevS, newS);
}

SymbolicTracker* ExternalBackingResource::GetStateTracker() {
    return m_impl->backing->GetStateTracker();
}

bool ExternalBackingResource::TryGetBufferByteSize(uint64_t& outByteSize) const {
    if (!m_impl || !m_impl->backing) {
        return false;
    }

    outByteSize = static_cast<uint64_t>(m_impl->backing->GetSize());
    return true;
}

ExternalBackingResource::ExternalBackingResource(std::unique_ptr<GpuBufferBacking> backing)
    : m_impl(std::make_unique<Impl>(std::move(backing)))
{
    m_hasLayout = false;
    m_mipLevels = 1;
    m_arraySize = 1;
}

ExternalBackingResource::~ExternalBackingResource() = default;
