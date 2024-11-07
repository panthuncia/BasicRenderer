#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <string>
#include <algorithm> // For std::lower_bound, std::upper_bound

#include "DeviceManager.h"
#include "Buffer.h"
#include "Resource.h"
#include "BufferHandle.h"
#include "DynamicBufferBase.h"

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
            Resize(m_capacity * 2);
            if (onResized) {
                onResized(m_globalResizableBufferID, sizeof(unsigned int), m_capacity, this);
            }
        }
        // Find the insertion point
        auto it = std::lower_bound(m_data.begin(), m_data.end(), element);
        size_t index = std::distance(m_data.begin(), it);

        // Insert the element
        m_data.insert(it, element);

        // Update the earliest modified index
        if (index < m_earliestModifiedIndex) {
            m_earliestModifiedIndex = index;
        }

    }

    // Remove an element
    void Remove(unsigned int element) {
        // Find the element
        auto it = std::lower_bound(m_data.begin(), m_data.end(), element);

        if (it != m_data.end() && *it == element) {
            size_t index = std::distance(m_data.begin(), it);

            // Remove the element
            m_data.erase(it);

            // Update the earliest modified index
            if (index < m_earliestModifiedIndex) {
                m_earliestModifiedIndex = index;
            }
        }
    }

    void UpdateUploadBuffer() {
        // Update upload buffer from insertion point onwards
        void* uploadData = nullptr;
        m_uploadBuffer->m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&uploadData));
        std::memcpy(reinterpret_cast<unsigned char*>(uploadData) + m_earliestModifiedIndex * sizeof(unsigned int), &m_data[m_earliestModifiedIndex], sizeof(unsigned int) * (m_data.size() - m_earliestModifiedIndex));
        m_uploadBuffer->m_buffer->Unmap(0, nullptr);
    }

    // Get element at index
    unsigned int& operator[](UINT index) {
        return m_data[index];
    }

    const unsigned int& operator[](UINT index) const {
        return m_data[index];
    }

    void Resize(UINT newCapacity) {
        if (newCapacity > m_capacity) {
            CreateBuffer(newCapacity);
            m_capacity = newCapacity;
        }
    }

    // Update only the modified portion of the buffer
    //bool UpdateUploadBuffer() {
    //    if (m_earliestModifiedIndex < m_data.size()) {
    //        // Calculate the byte offset and size to update
    //        size_t offset = m_earliestModifiedIndex * sizeof(unsigned int);
    //        size_t dataSize = (m_data.size() - m_earliestModifiedIndex) * sizeof(unsigned int);

    //        // Map the buffer
    //        unsigned int* pData = nullptr;
    //        D3D12_RANGE readRange = { 0, 0 };
    //        m_uploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));

    //        // Copy the modified data
    //        memcpy(reinterpret_cast<unsigned char*>(pData) + offset, m_data.data() + m_earliestModifiedIndex, dataSize);

    //        // Unmap the buffer
    //        m_uploadBuffer->m_buffer->Unmap(0, nullptr);

    //        // Reset the earliest modified index
    //        //m_earliestModifiedIndex = m_data.size();

    //        return true;
    //    }
    //    return false;
    //}

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
    std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState prevState, ResourceState newState) override {
        currentState = newState;
        return m_dataBuffer->GetTransitions(prevState, newState);
    }

private:
    SortedUnsignedIntBuffer(UINT id = 0, UINT capacity = 64, std::wstring name = L"", bool UAV = false)
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

    UINT m_capacity;
    size_t m_earliestModifiedIndex; // Tracks the earliest modified index

    UINT m_globalResizableBufferID;

    std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)> onResized;
    inline static std::wstring m_name = L"SortedUnsignedIntBuffer";

	bool m_UAV = false;

    // Create the GPU buffers
    void CreateBuffer(UINT capacity) {
        auto& device = DeviceManager::GetInstance().GetDevice();
        auto newUploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, sizeof(unsigned int) * capacity, true, m_UAV);

        void* mappedData;
        if (m_uploadBuffer != nullptr) {
            m_uploadBuffer->m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
            void* newMappedData = nullptr;
            newUploadBuffer->m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&newMappedData));
            std::memcpy(newMappedData, mappedData, sizeof(unsigned int) * m_capacity);
            newUploadBuffer->m_buffer->Unmap(0, nullptr);
            m_uploadBuffer->m_buffer->Unmap(0, nullptr);
        }
        m_uploadBuffer = newUploadBuffer;

        if (m_dataBuffer != nullptr) {
            DeletionManager::GetInstance().MarkForDelete(m_dataBuffer);
        }
        m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, sizeof(unsigned int) * capacity, false, m_UAV);
		SetName(name);
    }
};
