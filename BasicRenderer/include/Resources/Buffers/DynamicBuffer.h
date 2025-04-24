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
    void Deallocate(BufferView* view);
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
    std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState prevState, ResourceState newState) override {
        currentState = newState;
        return m_dataBuffer->GetTransitions(prevState, newState);
    }
    BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
        currentState = newState;
        return m_dataBuffer->GetEnhancedBarrierGroup(prevState, newState, prevAccessType, newAccessType, prevSyncState, newSyncState);
    }

private:
    DynamicBuffer(bool byteAddress, size_t elementSize, UINT id = 0, size_t size = 64*1024, std::wstring name = L"", bool UAV = false)
        : m_byteAddress(byteAddress), m_elementSize(elementSize), m_globalResizableBufferID(id), m_capacity(size), m_UAV(UAV), m_needsUpdate(false) {
        CreateBuffer(size);
        SetName(name);
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

    void SetState(ResourceState state) override {
        currentState = state;
        m_dataBuffer->SetState(state);
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