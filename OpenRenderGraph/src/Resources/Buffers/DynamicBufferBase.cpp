#include "Resources/Buffers/DynamicBufferBase.h"

#include <stdexcept>

#include "Managers/Singletons/DescriptorHeapManager.h"
#include "Resources/GPUBacking/GpuBufferBacking.h"
#include "Resources/ExternalBackingResource.h"
#include "Render/Runtime/UploadServiceAccess.h"

BufferBase::BufferBase() = default;

BufferBase::BufferBase(
    rhi::HeapType accessType,
    uint64_t bufferSize,
    bool unorderedAccess,
    bool materialize)
    : m_accessType(accessType)
    , m_bufferSize(bufferSize)
    , m_unorderedAccess(unorderedAccess)
{
    if (materialize && bufferSize > 0) {
        Materialize();
    }
}

BufferBase::~BufferBase() = default;

rhi::Resource BufferBase::GetAPIResource() {
    if (!m_dataBuffer) {
        throw std::runtime_error("Buffer resource is not materialized");
    }
    return m_dataBuffer->GetAPIResource();
}

SymbolicTracker* BufferBase::GetStateTracker() {
    if (!m_dataBuffer) {
        throw std::runtime_error("Buffer resource is not materialized");
    }
    return m_dataBuffer->GetStateTracker();
}

rhi::BarrierBatch BufferBase::GetEnhancedBarrierGroup(
    RangeSpec range,
    rhi::ResourceAccessType prevAccessType,
    rhi::ResourceAccessType newAccessType,
    rhi::ResourceLayout prevLayout,
    rhi::ResourceLayout newLayout,
    rhi::ResourceSyncState prevSyncState,
    rhi::ResourceSyncState newSyncState)
{
    if (!m_dataBuffer) {
        throw std::runtime_error("Buffer resource is not materialized");
    }
    return m_dataBuffer->GetEnhancedBarrierGroup(
        range,
        prevAccessType,
        newAccessType,
        prevLayout,
        newLayout,
        prevSyncState,
        newSyncState);
}

bool BufferBase::TryGetBufferByteSize(uint64_t& outByteSize) const {
    if (!m_dataBuffer) return false;
    outByteSize = static_cast<uint64_t>(m_dataBuffer->GetSize());
    return true;
}

void BufferBase::ConfigureBacking(
    rhi::HeapType accessType,
    uint64_t bufferSize,
    bool unorderedAccess)
{
    m_accessType = accessType;
    m_bufferSize = bufferSize;
    m_unorderedAccess = unorderedAccess;
}

bool BufferBase::IsMaterialized() const {
    return m_dataBuffer != nullptr;
}

uint64_t BufferBase::GetBufferSize() const {
    return m_bufferSize;
}

rhi::HeapType BufferBase::GetAccessType() const {
    return m_accessType;
}

bool BufferBase::IsUnorderedAccessEnabled() const {
    return m_unorderedAccess;
}

uint64_t BufferBase::GetBackingGeneration() const {
    return m_backingGeneration;
}

void BufferBase::Materialize(const MaterializeOptions* options) {
    if (m_dataBuffer) {
        return;
    }
    if (m_bufferSize == 0) {
        throw std::runtime_error("Cannot materialize a zero-sized buffer");
    }

    if (options && options->aliasPlacement.has_value()) {
        m_dataBuffer = GpuBufferBacking::CreateUnique(
            m_accessType,
            m_bufferSize,
            GetGlobalResourceID(),
            options->aliasPlacement.value(),
            m_unorderedAccess);
    }
    else {
        m_dataBuffer = GpuBufferBacking::CreateUnique(
            m_accessType,
            m_bufferSize,
            GetGlobalResourceID(),
            m_unorderedAccess);
    }

    RefreshDescriptorContents();
    ++m_backingGeneration;
    OnBackingMaterialized();
}

void BufferBase::Dematerialize() {
    if (!m_dataBuffer) {
        return;
    }
    m_dataBuffer.reset();
    ++m_backingGeneration;
}

void BufferBase::SetDescriptorRequirements(const DescriptorRequirements& requirements) {
    m_descriptorRequirements = requirements;

    EnsureVirtualDescriptorSlotsAllocated();
    if (m_dataBuffer) {
        RefreshDescriptorContents();
    }
}

bool BufferBase::HasDescriptorRequirements() const {
    return m_descriptorRequirements.has_value();
}

void BufferBase::EnsureVirtualDescriptorSlotsAllocated() {
    if (!m_descriptorRequirements.has_value() || HasAnyDescriptorSlots()) {
        return;
    }

    DescriptorHeapManager::ViewRequirements req{};
    DescriptorHeapManager::ViewRequirements::BufferViews views{};

    views.createCBV = m_descriptorRequirements->createCBV;
    views.createSRV = m_descriptorRequirements->createSRV;
    views.createUAV = m_descriptorRequirements->createUAV;
    views.createNonShaderVisibleUAV = m_descriptorRequirements->createNonShaderVisibleUAV;
    views.cbvDesc = m_descriptorRequirements->cbvDesc;
    views.srvDesc = m_descriptorRequirements->srvDesc;
    views.uavDesc = m_descriptorRequirements->uavDesc;
    views.uavCounterOffset = m_descriptorRequirements->uavCounterOffset;

    req.views = views;
    DescriptorHeapManager::GetInstance().ReserveDescriptorSlots(*this, req);
}

void BufferBase::RefreshDescriptorContents() {
    if (!m_descriptorRequirements.has_value() || !m_dataBuffer) {
        return;
    }

    EnsureVirtualDescriptorSlotsAllocated();

    DescriptorHeapManager::ViewRequirements req{};
    DescriptorHeapManager::ViewRequirements::BufferViews views{};

    views.createCBV = m_descriptorRequirements->createCBV;
    views.createSRV = m_descriptorRequirements->createSRV;
    views.createUAV = m_descriptorRequirements->createUAV;
    views.createNonShaderVisibleUAV = m_descriptorRequirements->createNonShaderVisibleUAV;
    views.cbvDesc = m_descriptorRequirements->cbvDesc;
    views.srvDesc = m_descriptorRequirements->srvDesc;
    views.uavDesc = m_descriptorRequirements->uavDesc;
    views.uavCounterOffset = m_descriptorRequirements->uavCounterOffset;

    req.views = views;
    auto resource = m_dataBuffer->GetAPIResource();
    DescriptorHeapManager::GetInstance().UpdateDescriptorContents(*this, resource, req);
}

void BufferBase::SetAliasingPool(uint64_t poolID) {
    m_aliasingPoolID = poolID;
    m_allowAlias = true;
}

void BufferBase::ClearAliasingPoolHint() {
    m_aliasingPoolID.reset();
}

std::optional<uint64_t> BufferBase::GetAliasingPoolHint() const {
    return m_aliasingPoolID;
}

void BufferBase::SetAllowAlias(bool allowAlias) {
    m_allowAlias = allowAlias;
}

bool BufferBase::IsAliasingAllowed() const {
    return m_allowAlias;
}

void BufferBase::SetBacking(std::unique_ptr<GpuBufferBacking> backing, uint64_t bufferSize) {
    m_dataBuffer = std::move(backing);
    m_bufferSize = bufferSize;
    RefreshDescriptorContents();
    ++m_backingGeneration;
    if (m_dataBuffer) {
        OnBackingMaterialized();
    }
}

void BufferBase::CreateAndSetBacking(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess) {
    auto newBacking = GpuBufferBacking::CreateUnique(
        accessType,
        bufferSize,
        GetGlobalResourceID(),
        unorderedAccess);
    SetBacking(std::move(newBacking), bufferSize);
}

void BufferBase::SetBackingName(const std::string& baseName, const std::string& suffix) {
    if (!m_dataBuffer) {
        return;
    }

    if (!suffix.empty()) {
        m_dataBuffer->SetName((baseName + ": " + suffix).c_str());
    }
    else {
        m_dataBuffer->SetName(baseName.c_str());
    }
}

void BufferBase::QueueResourceCopyFromOldBacking(uint64_t bytesToCopy) {
    if (!m_dataBuffer) {
        return;
    }

    auto oldBackingResource = ExternalBackingResource::CreateShared(std::move(m_dataBuffer));
    if (auto* uploadService = rg::runtime::GetActiveUploadService()) {
        uploadService->QueueResourceCopy(shared_from_this(), oldBackingResource, bytesToCopy);
    }
}

void BufferBase::ApplyMetadataToBacking(const EntityComponentBundle& bundle) {
    if (m_dataBuffer) {
        m_dataBuffer->ApplyMetadataComponentBundle(bundle);
    }
}
