#pragma once

#include <memory>
#include <optional>
#include <stdexcept>


#include "Render/RenderContext.h"
#include "Resources/GloballyIndexedResource.h"
#include "Resources/GPUBacking/GpuBufferBacking.h"

class BufferView;

class DynamicBufferBase : public GloballyIndexedResource {
public:
    struct MaterializeOptions {
        std::optional<BufferAliasPlacement> aliasPlacement;
    };

    DynamicBufferBase() {}

    DynamicBufferBase(
        rhi::HeapType accessType,
        uint64_t bufferSize,
        bool unorderedAccess = false)
        : m_accessType(accessType)
        , m_bufferSize(bufferSize)
        , m_unorderedAccess(unorderedAccess)
    {
        if (bufferSize > 0) {
            Materialize();
        }
    }

    rhi::Resource GetAPIResource() override {
        if (!m_dataBuffer) {
            throw std::runtime_error("Buffer resource is not materialized");
        }
        return m_dataBuffer->GetAPIResource();
    }

    SymbolicTracker* GetStateTracker() override {
        if (!m_dataBuffer) {
            throw std::runtime_error("Buffer resource is not materialized");
        }
        return m_dataBuffer->GetStateTracker();
    }

    rhi::BarrierBatch GetEnhancedBarrierGroup(
        RangeSpec range,
        rhi::ResourceAccessType prevAccessType,
        rhi::ResourceAccessType newAccessType,
        rhi::ResourceLayout prevLayout,
        rhi::ResourceLayout newLayout,
        rhi::ResourceSyncState prevSyncState,
        rhi::ResourceSyncState newSyncState) override
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

    bool TryGetBufferByteSize(uint64_t& outByteSize) const override {
        if (!m_dataBuffer) return false;
        outByteSize = static_cast<uint64_t>(m_dataBuffer->GetSize());
        return true;
    }

    void ConfigureBacking(
        rhi::HeapType accessType,
        uint64_t bufferSize,
        bool unorderedAccess = false)
    {
        m_accessType = accessType;
        m_bufferSize = bufferSize;
        m_unorderedAccess = unorderedAccess;
    }

    bool IsMaterialized() const {
        return m_dataBuffer != nullptr;
    }

    uint64_t GetBackingGeneration() const {
        return m_backingGeneration;
    }

    void Materialize(const MaterializeOptions* options = nullptr) {
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

        ++m_backingGeneration;
        OnBackingMaterialized();
    }

    void Dematerialize() {
        if (!m_dataBuffer) {
            return;
        }
        m_dataBuffer.reset();
        ++m_backingGeneration;
    }

protected:
    void SetBacking(std::unique_ptr<GpuBufferBacking> backing, uint64_t bufferSize) {
        m_dataBuffer = std::move(backing);
        m_bufferSize = bufferSize;
        ++m_backingGeneration;
        if (m_dataBuffer) {
            OnBackingMaterialized();
        }
    }

    void ApplyMetadataToBacking(const EntityComponentBundle& bundle) {
        if (m_dataBuffer) {
            m_dataBuffer->ApplyMetadataComponentBundle(bundle);
        }
    }

    virtual void OnBackingMaterialized() {}

	// Engine representation of a GPU buffer- owns a handle to the actual GPU resource.
    std::unique_ptr<GpuBufferBacking> m_dataBuffer = nullptr;
    rhi::HeapType m_accessType = rhi::HeapType::DeviceLocal;
    uint64_t m_bufferSize = 0;
    bool m_unorderedAccess = false;

private:
    uint64_t m_backingGeneration = 0;
};

class ViewedDynamicBufferBase : public DynamicBufferBase {
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