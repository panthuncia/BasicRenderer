#pragma once

#include <vector>
#include <functional>
#include <string>
#include <algorithm> // For std::lower_bound, std::upper_bound
#include <rhi.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"

using Microsoft::WRL::ComPtr;

class SortedUnsignedIntBuffer : public DynamicBufferBase {
public:
    static std::shared_ptr<SortedUnsignedIntBuffer> CreateShared(UINT id = 0, uint64_t capacity = 64, std::wstring name = L"", bool UAV = false) {
        return std::shared_ptr<SortedUnsignedIntBuffer>(new SortedUnsignedIntBuffer(id, capacity, name, UAV));
    }

    // Insert an element while maintaining sorted order (deduped)
    void Insert(unsigned int element) {
        // Resize the buffer if necessary
        if (m_data.size() >= m_capacity) {
            GrowBuffer(m_capacity * 2);
        }

        // Find the insertion point
        auto it = std::lower_bound(m_data.begin(), m_data.end(), element);
        // Prevent duplicates
        if (it != m_data.end() && *it == element) {
            return; // already present
        }

        uint32_t index = static_cast<uint32_t>(std::distance(m_data.begin(), it));
        m_data.insert(it, element);

        // Update the earliest modified index
        if (index < m_earliestModifiedIndex) {
            m_earliestModifiedIndex = index;
        }

        // Upload the entire suffix so GPU content matches the CPU vector after the insertion shift
        const unsigned int* src = m_data.data() + index;
        const uint32_t count = static_cast<uint32_t>(m_data.size() - index);
        QUEUE_UPLOAD(src, sizeof(unsigned int) * count, this, index * sizeof(unsigned int));
    }

    // Remove an element (and shift the tail on GPU)
    void Remove(unsigned int element) {
        // Find the element
        auto it = std::lower_bound(m_data.begin(), m_data.end(), element);

        if (it != m_data.end() && *it == element) {
            const uint32_t index = static_cast<uint32_t>(std::distance(m_data.begin(), it));

            // Erase from CPU
            m_data.erase(it);

            // Update the earliest modified index
            if (index < m_earliestModifiedIndex) {
                m_earliestModifiedIndex = index;
            }

            // Shift left in GPU: upload suffix starting at 'index'
            if (!m_data.empty() && index < m_data.size()) {
                const unsigned int* src = m_data.data() + index;
                const uint32_t count = static_cast<uint32_t>(m_data.size() - index);
                QUEUE_UPLOAD(src, sizeof(unsigned int) * count, this, index * sizeof(unsigned int));
            }

            // Optionally zero out the last stale slot (not strictly required if readers clamp to Size())
            if (m_data.size() < m_capacity) {
                const unsigned int zero = 0u;
                const uint32_t lastSlot = static_cast<uint32_t>(m_data.size());
                QUEUE_UPLOAD(&zero, sizeof(unsigned int), this, lastSlot * sizeof(unsigned int));
            }
        }
    }

    // Get element at index
    unsigned int& operator[](UINT index) {
        return m_data[index];
    }

    const unsigned int& operator[](UINT index) const {
        return m_data[index];
    }

    void SetOnResized(const std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)>& callback) {
        onResized = callback;
    }

    std::shared_ptr<Buffer>& GetBuffer() {
        return m_dataBuffer;
    }

    UINT Size() const {
        return static_cast<UINT>(m_data.size());
    }

    rhi::Resource GetAPIResource() override {
        return m_dataBuffer->GetAPIResource();
    }

protected:
    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {
        return m_dataBuffer->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
    }

private:
    SortedUnsignedIntBuffer(UINT id = 0, uint64_t capacity = 64, std::wstring name = L"", bool UAV = false)
        : m_globalResizableBufferID(id), m_capacity(capacity), m_UAV(UAV), m_earliestModifiedIndex(0) {
        CreateBuffer(capacity);
        SetName(name);
    }

    void OnSetName() override {
        if (name != L"") {
            m_dataBuffer->SetName((m_name + L": " + name).c_str());
        }
        else {
            m_dataBuffer->SetName(m_name.c_str());
        }
    }

    // Sorted list of unsigned integers
    std::vector<unsigned int> m_data;

    uint64_t m_capacity;
    uint64_t m_earliestModifiedIndex; // To avoid updating the entire buffer every time

    UINT m_globalResizableBufferID;

    std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)> onResized;
    inline static std::wstring m_name = L"SortedUnsignedIntBuffer";

    bool m_UAV = false;

    void CreateBuffer(uint64_t capacity) {
        auto device = DeviceManager::GetInstance().GetDevice();
        m_capacity = capacity;
        m_dataBuffer = Buffer::CreateShared(rhi::HeapType::DeviceLocal, capacity * sizeof(unsigned int), m_UAV);
    }

    void GrowBuffer(uint64_t newSize) {
        auto device = DeviceManager::GetInstance().GetDevice();
        if (m_dataBuffer != nullptr) {
            DeletionManager::GetInstance().MarkForDelete(m_dataBuffer);
        }
        auto newDataBuffer = Buffer::CreateShared(rhi::HeapType::DeviceLocal, newSize * sizeof(unsigned int), m_UAV);
        // Copy old content
        UploadManager::GetInstance().QueueResourceCopy(newDataBuffer, m_dataBuffer, m_capacity * sizeof(unsigned int));
        m_dataBuffer = newDataBuffer;

        m_capacity = newSize;
        onResized(m_globalResizableBufferID, static_cast<uint32_t>(sizeof(uint32_t)), static_cast<uint32_t>(m_capacity), this);
        SetName(name);
    }
};
