#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <typeinfo>
#include <string>

#include "Managers/Singletons/DeviceManager.h"
#include "Buffers/Buffer.h"
#include "Resource.h"
#include "Buffers/DynamicBufferBase.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Managers/Singletons/UploadManager.h"

using Microsoft::WRL::ComPtr;

template<class T>
class DynamicStructuredBuffer : public DynamicBufferBase {
public:

    static std::shared_ptr<DynamicStructuredBuffer<T>> CreateShared(UINT id = 0, UINT capacity = 64, std::wstring name = L"", bool UAV = false) {
        return std::shared_ptr<DynamicStructuredBuffer<T>>(new DynamicStructuredBuffer<T>(id, capacity, name, UAV));
    }

    unsigned int Add(const T& element) {
        if (m_data.size() >= m_capacity) {
            Resize(m_capacity * 2);
            onResized(m_globalResizableBufferID, sizeof(T), m_capacity, this);
        }
        m_data.push_back(element);
        m_needsUpdate = true;

        unsigned int index = m_data.size() - 1;

		UploadManager::GetInstance().UploadData(&element, sizeof(T), this, index * sizeof(T));

        return index;
    }

    void RemoveAt(UINT index) {
        if (index < m_data.size()) {
            m_data.erase(m_data.begin() + index);
            m_needsUpdate = true;
        }
    }

    T& operator[](UINT index) {
        return m_data[index];
    }

    const T& operator[](UINT index) const {
        return m_data[index];
    }

    void Resize(UINT newCapacity) {
        if (newCapacity > m_capacity) {
            CreateBuffer(newCapacity, m_capacity);
            m_capacity = newCapacity;
        }

    }

    void UpdateAt(UINT index, const T& element) {
		UploadManager::GetInstance().UploadData(&element, sizeof(T), this, index * sizeof(T));
    }

    void SetOnResized(const std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)>& callback) {
        onResized = callback;
    }

    std::shared_ptr<Buffer>& GetBuffer() {
        return m_dataBuffer;
    }

    UINT Size() {
        return m_data.size();
    }

	ID3D12Resource* GetAPIResource() const override { return m_dataBuffer->GetAPIResource(); }

protected:
    std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState prevState, ResourceState newState) override {
        currentState = newState;
        return m_dataBuffer->GetTransitions(prevState, newState);
    }

    BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
        currentState = newState;
        return m_dataBuffer->GetEnhancedBarrierGroup(prevState, newState, prevSyncState, newSyncState);
    }

private:
    DynamicStructuredBuffer(UINT id = 0, UINT capacity = 64, std::wstring name = L"", bool UAV = false)
        : m_globalResizableBufferID(id), m_capacity(capacity), m_UAV(UAV), m_needsUpdate(false) {
        CreateBuffer(capacity);
        if (name != L"") {
            m_dataBuffer->SetName((m_name + L": " + name).c_str());
        }
        else {
            m_dataBuffer->SetName(m_name.c_str());
        }
    }

    void OnSetName() override {
        if (name != L"") {
            m_dataBuffer->SetName((m_name + L": " + name).c_str());
        }
        else {
            m_dataBuffer->SetName(m_name.c_str());
        }
    }

    std::vector<T> m_data;
    size_t m_capacity;
    bool m_needsUpdate;

    UINT m_globalResizableBufferID;

    std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)> onResized;
    inline static std::wstring m_name = L"DynamicStructuredBuffer";

    bool m_UAV = false;

    void CreateBuffer(size_t capacity, size_t previousCapacity = 0) {
        auto& device = DeviceManager::GetInstance().GetDevice();

        auto newDataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, sizeof(T) * capacity, false, m_UAV);
        if (m_dataBuffer != nullptr) {
            UploadManager::GetInstance().QueueResourceCopy(newDataBuffer, m_dataBuffer, previousCapacity);
            DeletionManager::GetInstance().MarkForDelete(m_dataBuffer);
        }
		m_dataBuffer = newDataBuffer;
    }
};