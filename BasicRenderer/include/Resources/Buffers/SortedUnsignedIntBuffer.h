#pragma once

#include <vector>
#include <string>
#include <algorithm> // For std::lower_bound, std::upper_bound
#include <rhi.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Interfaces/IHasMemoryMetadata.h"

using Microsoft::WRL::ComPtr;

class SortedUnsignedIntBuffer : public DynamicBufferBase, public IHasMemoryMetadata {
public:
    static std::shared_ptr<SortedUnsignedIntBuffer> CreateShared(uint64_t capacity = 64, std::string name = "", bool UAV = false) {
        return std::shared_ptr<SortedUnsignedIntBuffer>(new SortedUnsignedIntBuffer(capacity, name, UAV));
    }

    // Insert an element while maintaining sorted order (deduped)
    void Insert(unsigned int element);

    // Remove an element (and shift the tail on GPU)
    void Remove(unsigned int element);

    // Get element at index
    unsigned int& operator[](UINT index) {
        return m_data[index];
    }

    const unsigned int& operator[](UINT index) const {
        return m_data[index];
    }

    UINT Size() const {
        return static_cast<UINT>(m_data.size());
    }

    rhi::Resource GetAPIResource() override {
        return m_dataBuffer->GetAPIResource();
    }

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) override {
        m_metadataBundles.emplace_back(bundle);
        m_dataBuffer->ApplyMetadataComponentBundle(bundle);
    }

    SymbolicTracker* GetStateTracker() override {
        return m_dataBuffer->GetStateTracker();
    }
protected:
    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {
        return m_dataBuffer->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
    }

private:
    SortedUnsignedIntBuffer(uint64_t capacity = 64, std::string name = "", bool UAV = false)
        : m_capacity(capacity), m_UAV(UAV), m_earliestModifiedIndex(0) {
        CreateBuffer(capacity);
        SetName(name);
    }

    void OnSetName() override {
        if (name != "") {
            m_dataBuffer->SetName((m_name + ": " + name).c_str());
        }
        else {
            m_dataBuffer->SetName(m_name.c_str());
        }
    }

    void AssignDescriptorSlots();

    // Sorted list of unsigned integers
    std::vector<unsigned int> m_data;

    uint64_t m_capacity;
    uint64_t m_earliestModifiedIndex; // To avoid updating the entire buffer every time

    std::vector<EntityComponentBundle> m_metadataBundles;

    inline static std::string m_name = "SortedUnsignedIntBuffer";

    bool m_UAV = false;

    void CreateBuffer(uint64_t capacity);

    void GrowBuffer(uint64_t newSize);
};
