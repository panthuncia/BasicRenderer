#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <typeinfo>
#include <string>
#include <concepts>
#include <memory>

#include "DeviceManager.h"
#include "Buffer.h"
#include "Resource.h"
#include "BufferHandle.h"
#include "DynamicBufferBase.h"
#include "Concepts/HasIsValid.h"
#include "BufferView.h"

using Microsoft::WRL::ComPtr;

class LazyDynamicStructuredBufferBase : public ViewedDynamicBufferBase { // Necessary to store these in a templateless vector
public:
	virtual size_t GetElementSize() const = 0;
};

template <HasIsValid T>  // Enforce the concept at the template parameter level
class LazyDynamicStructuredBuffer : public LazyDynamicStructuredBufferBase {
public:

    static inline constexpr size_t m_elementSize = sizeof(T);

	static std::shared_ptr<LazyDynamicStructuredBuffer<T>> CreateShared(UINT id = 0, UINT capacity = 64, std::wstring name = L"") {
		return std::shared_ptr<LazyDynamicStructuredBuffer<T>>(new LazyDynamicStructuredBuffer<T>(id, capacity, name));
	}

    std::unique_ptr<BufferView> Add() {
		if (!m_freeIndices.empty()) { // Reuse a free index
			unsigned int index = m_freeIndices.back();
			m_freeIndices.pop_back();
            return std::move(BufferView::CreateUnique(this, index * m_elementSize, m_elementSize, typeid(T)));
        }
        m_usedCapacity++;
		if (m_usedCapacity > m_capacity) { // Resize the buffer if necessary
            Resize(m_capacity * 2);
            onResized(m_globalResizableBufferID, m_elementSize, m_capacity, m_dataBuffer);
        }
		unsigned int index = m_usedCapacity - 1;
        return std::move(BufferView::CreateUnique(this, index * m_elementSize, m_elementSize, typeid(T)));
    }

    void Remove(std::unique_ptr<BufferView>& view) {
		T* element = reinterpret_cast<T*>(reinterpret_cast<char*>(m_mappedData) + view->GetOffset());
		element->isValid = false;
		MarkViewDirty(view.get());
		unsigned int index = view->GetOffset() / m_elementSize;
		m_freeIndices.push_back(index);
    }

    void Resize(UINT newCapacity) {
        if (newCapacity > m_capacity) {
            CreateBuffer(newCapacity);
            m_capacity = newCapacity;
        }
    }

    void UpdateAt(std::unique_ptr<BufferView>& view, const T& element) {
        T* element = reinterpret_cast<T*>(reinterpret_cast<char*>(m_mappedData) + view->GetOffset());
		*element = element;
		MarkViewDirty(view.get());
    }


    void SetOnResized(const std::function<void(UINT, UINT, UINT, std::shared_ptr<Buffer>&)>& callback) {
        onResized = callback;
    }

    std::shared_ptr<Buffer>& GetBuffer() {
        return m_dataBuffer;
    }

    UINT Size() {
        return m_usedCapacity;
    }

	size_t GetElementSize() const {
		return m_elementSize;
	}

	void* GetMappedData() {
		return m_mappedData;
	}

protected:
    void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) override {
		currentState = newState;
        m_dataBuffer->Transition(commandList, prevState, newState);
    }

private:
    LazyDynamicStructuredBuffer(UINT id = 0, UINT capacity = 64, std::wstring name = L"")
        : m_globalResizableBufferID(id), m_capacity(capacity), m_needsUpdate(false) {
        CreateBuffer(capacity);
        if (name != L"") {
            m_dataBuffer->SetName((m_name + L": " + name).c_str());
        }
        else {
            m_dataBuffer->SetName(m_name.c_str());
        }
    }

    UINT m_capacity;
	UINT m_usedCapacity = 0;
    bool m_needsUpdate;
	std::vector<unsigned int> m_freeIndices;
    void* m_mappedData = nullptr;
    UINT m_globalResizableBufferID;

    std::function<void(UINT, UINT, UINT, std::shared_ptr<Buffer>&)> onResized;
    inline static std::wstring m_name = L"DynamicStructuredBuffer";

    void CreateBuffer(UINT capacity) {
        auto& device = DeviceManager::GetInstance().GetDevice();
        m_uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, m_elementSize * capacity, true);
        m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, m_elementSize * capacity, false);
        CD3DX12_RANGE readRange(0, 0);
        m_uploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedData));
    }
};