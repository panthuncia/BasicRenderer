#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <vector>
#include <functional>
#include <typeinfo>
#include <string>
#include <concepts>
#include <memory>
#include <deque>

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Concepts/HasIsValid.h"
#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Managers/Singletons/UploadManager.h"

using Microsoft::WRL::ComPtr;

class LazyDynamicStructuredBufferBase : public ViewedDynamicBufferBase { // Necessary to store these in a templateless vector
public:
	virtual size_t GetElementSize() const = 0;
};

template <typename T>
class LazyDynamicStructuredBuffer : public LazyDynamicStructuredBufferBase {
public:

	static std::shared_ptr<LazyDynamicStructuredBuffer<T>> CreateShared(UINT id = 0, UINT capacity = 64, std::wstring name = L"", size_t alignment = 1, bool UAV = false) {
		return std::shared_ptr<LazyDynamicStructuredBuffer<T>>(new LazyDynamicStructuredBuffer<T>(id, capacity, name, alignment, UAV));
	}

    std::shared_ptr<BufferView> Add() {
		if (!m_freeIndices.empty()) { // Reuse a free index
			unsigned int index = m_freeIndices.front();
			m_freeIndices.pop_front();
            return std::move(BufferView::CreateShared(this, index * m_elementSize, m_elementSize, sizeof(T)));
        }
        m_usedCapacity++;
		if (m_usedCapacity > m_capacity) { // Resize the buffer if necessary
            Resize(m_capacity * 2);
            onResized(m_globalResizableBufferID, m_elementSize, m_capacity, this, m_UAV);
        }
		unsigned int index = m_usedCapacity - 1;
        return std::move(BufferView::CreateShared(this, index * m_elementSize, m_elementSize, sizeof(T)));
    }

	std::shared_ptr<BufferView> Add(const T& data) {
		auto view = Add();
		UpdateView(view.get(), &data);
		return view;
	}

    void Remove(BufferView* view) {
		unsigned int index = view->GetOffset() / m_elementSize;
		m_freeIndices.push_back(index);
    }

    void Resize(size_t newCapacity) {
        if (newCapacity > m_capacity) {
            CreateBuffer(newCapacity, m_capacity);
            m_capacity = newCapacity;
        }
    }

    void UpdateView(BufferView* view, const void* data) {
		auto& manager = UploadManager::GetInstance();
		manager.UploadData(data, sizeof(T), this, view->GetOffset());
    }


    void SetOnResized(const std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer, bool)>& callback) {
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

	ID3D12Resource* GetAPIResource() const override { return m_dataBuffer->GetAPIResource(); }

    virtual ResourceState GetState() const override { return m_dataBuffer->GetState(); }

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

    size_t m_capacity;
	size_t m_usedCapacity = 0;
    bool m_needsUpdate;
	std::deque<unsigned int> m_freeIndices;
    UINT m_globalResizableBufferID;
    size_t m_elementSize = 0;

    std::function<void(UINT, UINT, UINT, DynamicBufferBase* buffer, bool)> onResized;
    inline static std::wstring m_name = L"LazyDynamicStructuredBuffer";

    bool m_UAV = false;

    void CreateBuffer(size_t capacity, size_t previousCapacity = 0) {
        auto& device = DeviceManager::GetInstance().GetDevice();

        auto newDataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, m_elementSize * capacity, false, m_UAV);
        if (m_dataBuffer != nullptr) {
            UploadManager::GetInstance().QueueResourceCopy(newDataBuffer, m_dataBuffer, previousCapacity*sizeof(T));
            DeletionManager::GetInstance().MarkForDelete(m_dataBuffer);
        }
        m_dataBuffer = newDataBuffer;
        SetName(name);

    }
};