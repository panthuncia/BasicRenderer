#pragma once

#include <vector>
#include <functional>
#include <string>
#include <rhi.h>
#include <memory>

#include "Managers/Singletons/ResourceManager.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Managers/Singletons/UploadManager.h"

using Microsoft::WRL::ComPtr;

template<class T>
class DynamicStructuredBuffer : public DynamicBufferBase {
public:

    static std::shared_ptr<DynamicStructuredBuffer<T>> CreateShared(UINT capacity = 64, std::wstring name = L"", bool UAV = false) {
        return std::shared_ptr<DynamicStructuredBuffer<T>>(new DynamicStructuredBuffer<T>(capacity, name, UAV));
    }

    unsigned int Add(const T& element) {
        if (m_data.size() >= m_capacity) {
            Resize(m_capacity * 2);
        }
        m_data.push_back(element);
        m_needsUpdate = true;

        unsigned int index = static_cast<uint32_t>(m_data.size()) - 1; // TODO: Fix buffer max sizes

        BUFFER_UPLOAD(&element, sizeof(T), shared_from_this(), index * sizeof(T));

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

    void Resize(uint32_t newCapacity) {
        if (newCapacity > m_capacity) {
            CreateBuffer(newCapacity, m_capacity);
            m_capacity = newCapacity;
        }

    }

    void UpdateAt(UINT index, const T& element) {
        BUFFER_UPLOAD(&element, sizeof(T), shared_from_this(), index * sizeof(T));
    }

    std::shared_ptr<Buffer>& GetBuffer() {
        return m_dataBuffer;
    }

    UINT Size() {
        return static_cast<uint32_t>(m_data.size());
    }

	rhi::Resource GetAPIResource() override { return m_dataBuffer->GetAPIResource(); }

protected:

    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {
        return m_dataBuffer->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
    }

private:
    DynamicStructuredBuffer(UINT capacity = 64, std::wstring bufName = L"", bool UAV = false)
        : m_capacity(capacity), m_UAV(UAV), m_needsUpdate(false) {
		name = bufName;
        CreateBuffer(capacity);
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
    uint32_t m_capacity;
    bool m_needsUpdate;

    inline static std::wstring m_name = L"DynamicStructuredBuffer";

    bool m_UAV = false;

    void AssignDescriptorSlots(uint32_t capacity)
    {
        auto& rm = ResourceManager::GetInstance();

        ResourceManager::ViewRequirements req{};
        ResourceManager::ViewRequirements::BufferViews b{};

        b.createCBV = false;
        b.createSRV = true;
        b.createUAV = m_UAV;
        b.createNonShaderVisibleUAV = false;
        b.uavCounterOffset = 0;

        // SRV (structured buffer)
        b.srvDesc = rhi::SrvDesc{
        	.dimension = rhi::SrvDim::Buffer,
        	.formatOverride = rhi::Format::Unknown,
            .buffer = {
                .kind = rhi::BufferViewKind::Structured,
                .firstElement = 0,
                .numElements = capacity,
                .structureByteStride = static_cast<uint32_t>(sizeof(T)),
            },
        };

        // UAV (structured buffer), no counter
        b.uavDesc = rhi::UavDesc{
        	.dimension = rhi::UavDim::Buffer,
        	.formatOverride = rhi::Format::Unknown,
            .buffer = {
                .kind = rhi::BufferViewKind::Structured,
                .firstElement = 0,
                .numElements = capacity,
                .structureByteStride = static_cast<uint32_t>(sizeof(T)),
                .counterOffsetInBytes = 0,
            },
        };


        req.views = b;
        auto resource = m_dataBuffer->GetAPIResource();
        rm.AssignDescriptorSlots(*this, resource, req);
    }


    void CreateBuffer(size_t capacity, size_t previousCapacity = 0) {
        auto newDataBuffer = Buffer::CreateShared(rhi::HeapType::DeviceLocal, sizeof(T) * capacity, m_UAV);
		newDataBuffer->SetName((m_name+ L": " + name).c_str());
        if (m_dataBuffer != nullptr) {
            UploadManager::GetInstance().QueueResourceCopy(newDataBuffer, m_dataBuffer, previousCapacity);
        }
		m_dataBuffer = newDataBuffer;
        AssignDescriptorSlots(static_cast<uint32_t>(capacity));
    }
};