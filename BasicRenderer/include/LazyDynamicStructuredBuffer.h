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
#include "DeletionManager.h"

using Microsoft::WRL::ComPtr;

class LazyDynamicStructuredBufferBase : public ViewedDynamicBufferBase { // Necessary to store these in a templateless vector
public:
	virtual size_t GetElementSize() const = 0;
};

template <HasIsValid T>  // Enforce the concept at the template parameter level
class LazyDynamicStructuredBuffer : public LazyDynamicStructuredBufferBase {
public:

	static std::shared_ptr<LazyDynamicStructuredBuffer<T>> CreateShared(UINT id = 0, UINT capacity = 64, std::wstring name = L"", size_t alignment = 1, bool UAV = false) {
		return std::shared_ptr<LazyDynamicStructuredBuffer<T>>(new LazyDynamicStructuredBuffer<T>(id, capacity, name, alignment, UAV));
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
            onResized(m_globalResizableBufferID, m_elementSize, m_capacity, this);
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

    void UpdateAt(std::unique_ptr<BufferView>& view, const T& data) {
        T* element = reinterpret_cast<T*>(reinterpret_cast<char*>(m_mappedData) + view->GetOffset());
		*element = data;
		MarkViewDirty(view.get());
    }


    void SetOnResized(const std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)>& callback) {
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

	ID3D12Resource* GetAPIResource() const override { return m_dataBuffer->GetAPIResource(); }

protected:
    void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) override {
		currentState = newState;
        m_dataBuffer->Transition(commandList, prevState, newState);
    }

private:
    LazyDynamicStructuredBuffer(UINT id = 0, UINT capacity = 64, std::wstring name = L"", size_t alignment = 1, bool UAV = false)
        : m_globalResizableBufferID(id), m_capacity(capacity), m_UAV(UAV), m_needsUpdate(false) {
		m_elementSize = ((sizeof(T) + alignment - 1) / alignment) * alignment;
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

    UINT m_capacity;
	UINT m_usedCapacity = 0;
    bool m_needsUpdate;
	std::vector<unsigned int> m_freeIndices;
    void* m_mappedData = nullptr;
    UINT m_globalResizableBufferID;
    size_t m_elementSize = 0;

    std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer)> onResized;
    inline static std::wstring m_name = L"LazyDynamicStructuredBuffer";

    bool m_UAV = false;

    void CreateBuffer(UINT capacity) {
        auto& device = DeviceManager::GetInstance().GetDevice();
        auto newUploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, m_elementSize * capacity, true, false);
        if (m_uploadBuffer != nullptr) {
            void* newMappedData = nullptr;
			newUploadBuffer->m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&newMappedData));
			std::memcpy(newMappedData, m_mappedData, m_elementSize * m_capacity);
            newUploadBuffer->m_buffer->Unmap(0, nullptr);
        }
		m_uploadBuffer = newUploadBuffer;

        if (m_dataBuffer != nullptr) {
			DeletionManager::GetInstance().MarkForDelete(m_dataBuffer);
        }
        m_dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, m_elementSize * capacity, false, m_UAV);
        SetName(name);
        CD3DX12_RANGE readRange(0, 0);
        m_uploadBuffer->m_buffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedData));
    }
};