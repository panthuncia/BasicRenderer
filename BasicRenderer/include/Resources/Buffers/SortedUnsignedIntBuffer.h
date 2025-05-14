#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <string>
#include <algorithm> // For std::lower_bound, std::upper_bound

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"

using Microsoft::WRL::ComPtr;

class SortedUnsignedIntBuffer : public DynamicBufferBase {
public:
    static std::shared_ptr<SortedUnsignedIntBuffer> CreateShared(UINT id = 0, UINT capacity = 64, std::wstring name = L"", bool UAV = false) {
        return std::shared_ptr<SortedUnsignedIntBuffer>(new SortedUnsignedIntBuffer(id, capacity, name, UAV));
    }

    // Insert an element while maintaining sorted order
    void Insert(unsigned int element) {
        // Resize the buffer if necessary
        if (m_data.size() >= m_capacity) {
            GrowBuffer(m_capacity * 2);
        }
        // Find the insertion point
        auto it = std::lower_bound(m_data.begin(), m_data.end(), element);
        size_t index = std::distance(m_data.begin(), it);

        m_data.insert(it, element);

        // Update the earliest modified index
        if (index < m_earliestModifiedIndex) {
            m_earliestModifiedIndex = index;
        }
		UploadManager::GetInstance().UploadData(&element, sizeof(unsigned int), this, index * sizeof(unsigned int));

    }

    // Remove an element
    void Remove(unsigned int element) {
        // Find the element
        auto it = std::lower_bound(m_data.begin(), m_data.end(), element);

        if (it != m_data.end() && *it == element) {
            size_t index = std::distance(m_data.begin(), it);

            m_data.erase(it);

            // Update the earliest modified index
            if (index < m_earliestModifiedIndex) {
                m_earliestModifiedIndex = index;
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

	ID3D12Resource* GetAPIResource() const override {
		return m_dataBuffer->GetAPIResource();
	}

protected:

    BarrierGroups& GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
        m_subresourceAccessTypes[0] = newAccessType;
        m_subresourceLayouts[0] = newLayout;
        m_subresourceSyncStates[0] = newSyncState;
        return m_dataBuffer->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
    }

private:
    SortedUnsignedIntBuffer(UINT id = 0, UINT capacity = 64, std::wstring name = L"", bool UAV = false)
        : m_globalResizableBufferID(id), m_capacity(capacity), m_UAV(UAV), m_earliestModifiedIndex(0) {
        CreateBuffer(capacity);
        SetName(name);
        m_subresourceAccessTypes.push_back(ResourceAccessType::COMMON);
        m_subresourceLayouts.push_back(ResourceLayout::LAYOUT_COMMON);
        m_subresourceSyncStates.push_back(ResourceSyncState::ALL);
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

    size_t m_capacity;
	size_t m_earliestModifiedIndex; // To avoid updating the entire buffer every time

    UINT m_globalResizableBufferID;

    std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)> onResized;
    inline static std::wstring m_name = L"SortedUnsignedIntBuffer";

	bool m_UAV = false;

    void CreateBuffer(size_t capacity) {
		auto& device = DeviceManager::GetInstance().GetDevice();
		m_capacity = capacity;
		m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, capacity * sizeof(unsigned int), false, m_UAV);
    }

    void GrowBuffer(size_t newSize) {
		auto& device = DeviceManager::GetInstance().GetDevice();
		if (m_dataBuffer != nullptr) {
			DeletionManager::GetInstance().MarkForDelete(m_dataBuffer);
		}
		auto newDataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, newSize * sizeof(unsigned int), false, m_UAV);
		UploadManager::GetInstance().QueueResourceCopy(newDataBuffer, m_dataBuffer, m_capacity*sizeof(unsigned int));
		m_dataBuffer = newDataBuffer;

		size_t oldCapacity = m_capacity;
		size_t sizeDiff = newSize - m_capacity;
		m_capacity = newSize;
		onResized(m_globalResizableBufferID, sizeof(unsigned int), m_capacity, this);
		SetName(name);
    }
};
