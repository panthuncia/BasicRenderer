#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rhi.h>

#include "Resources/AliasingPlacement.h"
#include "Resources/GloballyIndexedResource.h"
#include "Resources/TrackedAllocation.h"

class GpuBufferBacking;

class BufferView;

class BufferBase : public GloballyIndexedResource {
public:
    struct MaterializeOptions {
        std::optional<BufferAliasPlacement> aliasPlacement;
    };

    struct DescriptorRequirements {
        bool createCBV = false;
        bool createSRV = false;
        bool createUAV = false;
        bool createNonShaderVisibleUAV = false;

        rhi::CbvDesc cbvDesc{};
        rhi::SrvDesc srvDesc{};
        rhi::UavDesc uavDesc{};

        uint64_t uavCounterOffset = 0;
    };

    BufferBase();

    BufferBase(
        rhi::HeapType accessType,
        uint64_t bufferSize,
        bool unorderedAccess = false,
        bool materialize = true);

    rhi::Resource GetAPIResource() override;

    SymbolicTracker* GetStateTracker() override;

    rhi::BarrierBatch GetEnhancedBarrierGroup(
        RangeSpec range,
        rhi::ResourceAccessType prevAccessType,
        rhi::ResourceAccessType newAccessType,
        rhi::ResourceLayout prevLayout,
        rhi::ResourceLayout newLayout,
        rhi::ResourceSyncState prevSyncState,
        rhi::ResourceSyncState newSyncState) override;

    bool TryGetBufferByteSize(uint64_t& outByteSize) const override;

    void ConfigureBacking(
        rhi::HeapType accessType,
        uint64_t bufferSize,
        bool unorderedAccess = false)
    ;

    bool IsMaterialized() const;

    uint64_t GetBufferSize() const;

    rhi::HeapType GetAccessType() const;

    bool IsUnorderedAccessEnabled() const;

    uint64_t GetBackingGeneration() const;

    void Materialize(const MaterializeOptions* options = nullptr);

    void Dematerialize();

    void SetDescriptorRequirements(const DescriptorRequirements& requirements);

    bool HasDescriptorRequirements() const;

    void EnsureVirtualDescriptorSlotsAllocated();

    void RefreshDescriptorContents();

    void SetAliasingPool(uint64_t poolID);

    void ClearAliasingPoolHint();

    std::optional<uint64_t> GetAliasingPoolHint() const;

    void SetAllowAlias(bool allowAlias);

    bool IsAliasingAllowed() const;

    virtual ~BufferBase();

protected:
    void SetBacking(std::unique_ptr<GpuBufferBacking> backing, uint64_t bufferSize);
    void CreateAndSetBacking(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess);
    void SetBackingName(const std::string& baseName, const std::string& suffix);
    void QueueResourceCopyFromOldBacking(uint64_t bytesToCopy);

    void ApplyMetadataToBacking(const EntityComponentBundle& bundle);

    virtual void OnBackingMaterialized() {}

	// Engine representation of a GPU buffer- owns a handle to the actual GPU resource.
    std::unique_ptr<GpuBufferBacking> m_dataBuffer = nullptr;
    rhi::HeapType m_accessType = rhi::HeapType::DeviceLocal;
    uint64_t m_bufferSize = 0;
    bool m_unorderedAccess = false;
    std::optional<DescriptorRequirements> m_descriptorRequirements;
    bool m_allowAlias = false;
    std::optional<uint64_t> m_aliasingPoolID;

private:
    uint64_t m_backingGeneration = 0;
};

class ViewedDynamicBufferBase : public BufferBase {
public:
    ViewedDynamicBufferBase() {}

    void MarkViewDirty(BufferView* view) {
        m_dirtyViews.push_back(view);
    }

	void ClearDirtyViews() {
		m_dirtyViews.clear();
	}

    const std::vector<BufferView*>& GetDirtyViews() {
        return m_dirtyViews;
    }

    virtual void UpdateView(BufferView* view, const void* data) = 0;

protected:
    std::vector<BufferView*> m_dirtyViews;
};