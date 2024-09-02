#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>

#include "DeviceManager.h"

using Microsoft::WRL::ComPtr;

template<class T>
class DynamicStructuredBuffer {
public:
    //template<typename T>
    DynamicStructuredBuffer(UINT id = 0, UINT capacity = 64)
        : m_globalResizableBufferID(id), m_capacity(capacity), m_needsUpdate(false) {
        CreateBuffer(capacity);
    }

    void Add(const T& element) {
        if (m_data.size() >= m_capacity) {
            Resize(m_capacity * 2);
            onResized(m_globalResizableBufferID, sizeof(T), m_capacity, m_buffer);
        }
        m_data.push_back(element);
        m_needsUpdate = true;
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
    }

    void UpdateBuffer() {
        if (m_needsUpdate) {
            // Map buffer and copy data
            T* pData = nullptr;
            D3D12_RANGE readRange = { 0, 0 }; // We do not intend to read from this resource on the CPU.
            m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
            memcpy(pData, m_data.data(), sizeof(T) * m_data.size());
            m_buffer->Unmap(0, nullptr);

            m_needsUpdate = false;
        }
    }

    void SetOnResized(const std::function<void(UINT, UINT, UINT, ComPtr<ID3D12Resource>&)>& callback) {
        onResized = callback;
    }

    ComPtr<ID3D12Resource>& GetBuffer() {
        return m_buffer;
    }

    UINT Size() {
        return m_data.size();
    }

private:
    ComPtr<ID3D12Resource> m_buffer;
    std::vector<T> m_data;
    UINT m_capacity;
    bool m_needsUpdate;

    UINT m_globalResizableBufferID;

    std::function<void(UINT, UINT, UINT, ComPtr<ID3D12Resource>&)> onResized;

    void CreateBuffer(UINT capacity) {
        // Create the buffer
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeof(T) * capacity;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        auto& device = DeviceManager::GetInstance().GetDevice();
        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_buffer)
        );
    }
};