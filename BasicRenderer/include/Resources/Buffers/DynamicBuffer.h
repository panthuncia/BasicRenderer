#pragma once

#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <typeinfo>
#include <string>
#include <typeinfo>
#include <typeindex>

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Resources/Buffers/MemoryBlock.h"

using Microsoft::WRL::ComPtr;

class BufferView;

class DynamicBuffer : public ViewedDynamicBufferBase {
public:

    static std::shared_ptr<DynamicBuffer> CreateShared(bool byteAddress, size_t elementSize, UINT id = 0, size_t capacity = 64, std::wstring name = L"", bool UAV = false) {
        return std::shared_ptr<DynamicBuffer>(new DynamicBuffer(byteAddress, elementSize, id, capacity, name, UAV));
    }

    std::unique_ptr<BufferView> Allocate(size_t size, size_t elementSize);
    void Deallocate(const BufferView* view);
	std::unique_ptr<BufferView> AddData(const void* data, size_t size, size_t elementSize);
	void UpdateView(BufferView* view, const void* data);

    void SetOnResized(const std::function<void(UINT, size_t, size_t, bool, DynamicBufferBase*, bool)>& callback) {
        onResized = callback;
    }

    std::shared_ptr<Buffer>& GetBuffer() {
        return m_dataBuffer;
    }

    size_t Size() {
        return m_capacity;
    }

	void* GetMappedData() {
		return m_mappedData;
	}

	ID3D12Resource* GetAPIResource() const override { return m_dataBuffer->GetAPIResource(); }

protected:
    BarrierGroups GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
        m_subresourceAccessTypes[0] = newAccessType;
        m_subresourceLayouts[0] = newLayout;
        m_subresourceSyncStates[0] = newSyncState;
        return m_dataBuffer->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
    }

private:
    DynamicBuffer(bool byteAddress, size_t elementSize, UINT id = 0, size_t size = 64*1024, std::wstring name = L"", bool UAV = false)
        : m_byteAddress(byteAddress), m_elementSize(elementSize), m_globalResizableBufferID(id), m_capacity(size), m_UAV(UAV), m_needsUpdate(false) {
        CreateBuffer(size);
        SetName(name);
		m_subresourceAccessTypes.push_back(ResourceAccessType::COMMON);
		m_subresourceLayouts.push_back(ResourceLayout::LAYOUT_COMMON);
		m_subresourceSyncStates.push_back(ResourceSyncState::ALL);
    }

    void OnSetName() override {
        if (name != L"") {
			m_name = name;
			std::wstring name = m_baseName + L": " + m_name;
            m_dataBuffer->SetName(name.c_str());
        }
        else {
            m_dataBuffer->SetName(m_baseName.c_str());
        }
    }

	size_t m_elementSize;
	bool m_byteAddress;

    void* m_mappedData = nullptr;

    size_t m_capacity;
    bool m_needsUpdate;

    UINT m_globalResizableBufferID;

    std::vector<MemoryBlock> m_memoryBlocks;

    std::function<void(UINT, size_t, size_t, bool, DynamicBufferBase* buffer, bool)> onResized;
    inline static std::wstring m_baseName = L"DynamicBuffer";
	std::wstring m_name = m_baseName;

    bool m_UAV = false;

    void CreateBuffer(size_t capacity);
    void GrowBuffer(size_t newSize);
};