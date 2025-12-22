#pragma once

#include <vector>
#include <string>
#include <memory>
#include <deque>
#include <rhi.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/GpuBufferBacking.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/UploadManager.h"

using Microsoft::WRL::ComPtr;

class LazyDynamicStructuredBufferBase : public ViewedDynamicBufferBase { // Necessary to store these in a templateless vector
public:
	virtual size_t GetElementSize() const = 0;
    virtual void UpdateView(BufferView* view, const void* data) = 0;
};

template <typename T>
class LazyDynamicStructuredBuffer : public LazyDynamicStructuredBufferBase {
public:

	static std::shared_ptr<LazyDynamicStructuredBuffer<T>> CreateShared(UINT capacity = 64, std::string name = "", uint64_t alignment = 1, bool UAV = false) {
		return std::shared_ptr<LazyDynamicStructuredBuffer<T>>(new LazyDynamicStructuredBuffer<T>(capacity, name, alignment, UAV));
	}

    std::shared_ptr<BufferView> Add() {
        auto viewedWeak = std::weak_ptr<ViewedDynamicBufferBase>(
            std::dynamic_pointer_cast<ViewedDynamicBufferBase>(Resource::weak_from_this().lock())
        );
		if (!m_freeIndices.empty()) { // Reuse a free index
			uint64_t index = m_freeIndices.front();
			m_freeIndices.pop_front();
            return BufferView::CreateShared(viewedWeak, index * m_elementSize, m_elementSize, sizeof(T));
        }
        m_usedCapacity++;
		if (m_usedCapacity > m_capacity) { // Resize the buffer if necessary
            Resize(m_capacity * 2);
        }
		size_t index = m_usedCapacity - 1;
        return BufferView::CreateShared(viewedWeak, index * m_elementSize, m_elementSize, sizeof(T));
    }

	std::shared_ptr<BufferView> Add(const T& data) {
		auto view = Add();
		UpdateView(view.get(), &data);
		return view;
	}

    void Remove(BufferView* view) {
		uint64_t index = view->GetOffset() / m_elementSize;
		m_freeIndices.push_back(index);
    }

    void Resize(uint32_t newCapacity) {
        if (newCapacity > m_capacity) {
            CreateBuffer(newCapacity, m_capacity);
            m_capacity = newCapacity;
        }
    }

    void UpdateView(BufferView* view, const void* data) {
        BUFFER_UPLOAD(data, sizeof(T), shared_from_this(), view->GetOffset());
    }

    UINT Size() {
        return m_usedCapacity;
    }

	size_t GetElementSize() const {
		return m_elementSize;
	}

	rhi::Resource GetAPIResource() override { return m_dataBuffer->GetAPIResource(); }


protected:

    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {
        return m_dataBuffer->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
    }

private:
    LazyDynamicStructuredBuffer(UINT capacity = 64, std::string name = "", uint64_t alignment = 1, bool UAV = false)
        : m_capacity(capacity), m_UAV(UAV), m_needsUpdate(false) {
        if (alignment == 0) {
			alignment = 1;
        }
		m_elementSize = static_cast<uint32_t>(((sizeof(T) + alignment - 1) / alignment) * alignment);
        CreateBuffer(capacity);
		SetName(name);
    }
    void OnSetName() override {
        if (name != "") {
            m_dataBuffer->SetName((m_name + ": " + name).c_str());
        }
        else {
            m_dataBuffer->SetName(m_name.c_str());
        }
    }

    uint32_t m_capacity;
    uint64_t m_usedCapacity = 0;
    bool m_needsUpdate;
	std::deque<uint64_t> m_freeIndices;
    uint32_t m_elementSize = 0;

    inline static std::string m_name = "LazyDynamicStructuredBuffer";

    bool m_UAV = false;

    void AssignDescriptorSlots(uint32_t newCapacity)
    {
        auto& rm = ResourceManager::GetInstance();

        ResourceManager::ViewRequirements req{};
        ResourceManager::ViewRequirements::BufferViews b{};

        b.createCBV = false;
        b.createSRV = true;
        b.createUAV = m_UAV;
        b.createNonShaderVisibleUAV = false;
        b.uavCounterOffset = 0;

        // SRV (structured)
        b.srvDesc = rhi::SrvDesc{
            .dimension = rhi::SrvDim::Buffer,
            .formatOverride = rhi::Format::Unknown,
            .buffer = {
                .kind = rhi::BufferViewKind::Structured,
                .firstElement = 0,
                .numElements = newCapacity,
                .structureByteStride = static_cast<uint32_t>(sizeof(T)),
            },
        };

        // UAV (structured), no counter
        b.uavDesc = rhi::UavDesc{
            .dimension = rhi::UavDim::Buffer,
            .formatOverride = rhi::Format::Unknown,
            .buffer = {
                .kind = rhi::BufferViewKind::Structured,
                .firstElement = 0,
                .numElements = newCapacity,
                .structureByteStride = static_cast<uint32_t>(sizeof(T)),
                .counterOffsetInBytes = 0,
            },
        };

        req.views = b;
        auto resource = m_dataBuffer->GetAPIResource();
        rm.AssignDescriptorSlots(*this, resource, req);
    }

    void CreateBuffer(uint64_t capacity, size_t previousCapacity = 0) {
        auto newDataBuffer = GpuBufferBacking::CreateUnique(rhi::HeapType::DeviceLocal, m_elementSize * capacity, GetGlobalResourceID(), m_UAV);
        if (m_dataBuffer != nullptr) {
            UploadManager::GetInstance().QueueCopyAndDiscard(shared_from_this(), std::move(m_dataBuffer), *GetStateTracker(), previousCapacity*sizeof(T));
        }
        m_dataBuffer = std::move(newDataBuffer);

        AssignDescriptorSlots(static_cast<uint32_t>(capacity));

        SetName(name);

    }
};