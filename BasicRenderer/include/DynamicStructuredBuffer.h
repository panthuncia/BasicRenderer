#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <typeinfo>
#include <string>

#include "DeviceManager.h"
#include "Buffer.h"
#include "Resource.h"
#include "BufferHandle.h"

using Microsoft::WRL::ComPtr;

class DynamicBufferBase : public Resource{
public:
    DynamicBufferBase() {}
    std::shared_ptr<Buffer> m_uploadBuffer;
	std::shared_ptr<Buffer> m_dataBuffer;
protected:
    virtual void Transition(const RenderContext& context, ResourceState prevState, ResourceState newState) {};
};

template<class T>
class DynamicStructuredBuffer : public DynamicBufferBase {
public:

	static std::shared_ptr<DynamicStructuredBuffer<T>> CreateShared(UINT id = 0, UINT capacity = 64, std::wstring name = L"") {
		return std::shared_ptr<DynamicStructuredBuffer<T>>(new DynamicStructuredBuffer<T>(id, capacity, name));
	}

    void Add(const T& element) {
        if (m_data.size() >= m_capacity) {
            Resize(m_capacity * 2);
            onResized(m_globalResizableBufferID, sizeof(T), m_capacity, m_dataBuffer);
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
        m_needsUpdate = true;
    }

    bool UpdateUploadBuffer() {
        if (m_needsUpdate) {
            // Map buffer and copy data
            T* pData = nullptr;
            D3D12_RANGE readRange = { 0, 0 }; // We do not intend to read from this resource on the CPU.
            m_uploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
            memcpy(pData, m_data.data(), sizeof(T) * m_data.size());
            m_uploadBuffer->m_buffer->Unmap(0, nullptr);

            m_needsUpdate = false;
            return true;
        }
        return false;
    }

    void SetOnResized(const std::function<void(UINT, UINT, UINT, std::shared_ptr<Buffer>&)>& callback) {
        onResized = callback;
    }

    std::shared_ptr<Buffer>& GetBuffer() {
        return m_dataBuffer;
    }

    UINT Size() {
        return m_data.size();
    }
protected:
    void Transition(const RenderContext& context, ResourceState prevState, ResourceState newState) override {
		currentState = newState;
        m_dataBuffer->Transition(context, prevState, newState);
    }

private:
    DynamicStructuredBuffer(UINT id = 0, UINT capacity = 64, std::wstring name = L"")
        : m_globalResizableBufferID(id), m_capacity(capacity), m_needsUpdate(false) {
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

    std::function<void(UINT, UINT, UINT, std::shared_ptr<Buffer>&)> onResized;
    inline static std::wstring m_name = L"DynamicStructuredBuffer"; //s2ws(std::string(typeid(T).name())) + L">";

    void CreateBuffer(UINT capacity) {
        // Create the buffer
        auto& device = DeviceManager::GetInstance().GetDevice();
        m_uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, sizeof(T) * capacity, true);
        m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, sizeof(T) * capacity, false);
    }
};