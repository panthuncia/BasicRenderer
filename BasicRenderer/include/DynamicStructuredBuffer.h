#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <typeinfo>
#include <string>

#include "DeviceManager.h"
#include "Buffer.h"
#include "Resource.h"
#include "BufferHandle.h"
#include "DynamicBufferBase.h"

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
		return m_data.size() - 1;
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
            CreateBuffer(newCapacity);
            m_capacity = newCapacity;
        }

    }

    void UpdateAt(UINT index, const T& element) {
        m_data[index] = element;
        m_needsUpdate = true;
    }

    bool UpdateUploadBuffer() {
        if (m_needsUpdate) {
            // Map buffer and copy data
            T* pData = nullptr;
            D3D12_RANGE readRange = { 0, 0 };
            m_uploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
            memcpy(pData, m_data.data(), sizeof(T) * m_data.size());
            m_uploadBuffer->m_buffer->Unmap(0, nullptr);

            m_needsUpdate = false;
            return true;
        }
        return false;
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
protected:
    void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) override {
        currentState = newState;
        m_dataBuffer->Transition(commandList, prevState, newState);
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

    std::vector<T> m_data;
    UINT m_capacity;
    bool m_needsUpdate;

    UINT m_globalResizableBufferID;

    std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)> onResized;
    inline static std::wstring m_name = L"DynamicStructuredBuffer";

    bool m_UAV = false;

    void CreateBuffer(UINT capacity) {
        auto& device = DeviceManager::GetInstance().GetDevice();
        m_uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, sizeof(T) * capacity, true, false);
        m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, sizeof(T) * capacity, false, m_UAV);
    }
};