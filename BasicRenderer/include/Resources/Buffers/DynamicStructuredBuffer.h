#pragma once

#include <vector>
#include <functional>
#include <string>
#include <rhi.h>
#include <memory>

#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Interfaces/IHasMemoryMetadata.h"
#include "Render/Runtime/UploadServiceAccess.h"

using Microsoft::WRL::ComPtr;

template<class T>
class DynamicStructuredBuffer : public BufferBase, public IHasMemoryMetadata {
public:

    static std::shared_ptr<DynamicStructuredBuffer<T>> CreateShared(UINT capacity = 64, std::string name = "", bool UAV = false) {
        return std::shared_ptr<DynamicStructuredBuffer<T>>(new DynamicStructuredBuffer<T>(capacity, name, UAV));
    }

    unsigned int Add(const T& element) {
        if (m_data.size() >= m_capacity) {
            Resize(m_capacity * 2);
        }
        m_data.push_back(element);

        unsigned int index = static_cast<uint32_t>(m_data.size()) - 1; // TODO: Fix buffer max sizes

        BUFFER_UPLOAD(&element, sizeof(T), rg::runtime::UploadTarget::FromShared(shared_from_this()), index * sizeof(T));

        return index;
    }

    void RemoveAt(UINT index) {
        if (index < m_data.size()) {
            m_data.erase(m_data.begin() + index);

			// If capacity is half or less, shrink the buffer
            if (m_data.size() <= m_capacity / 2 && m_capacity > 64) {
				auto newCapacity = m_capacity / 2;
				Resize(newCapacity);
			}

			// batch upload data after the removed index
			unsigned int countToUpload = static_cast<unsigned int>(m_data.size()) - index;
            if (countToUpload > 0) {
                BUFFER_UPLOAD(&m_data[index], sizeof(T) * countToUpload, rg::runtime::UploadTarget::FromShared(shared_from_this()), index * sizeof(T));
            }
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
        BUFFER_UPLOAD(&element, sizeof(T), rg::runtime::UploadTarget::FromShared(shared_from_this()), index * sizeof(T));
    }

    UINT Size() {
        return static_cast<uint32_t>(m_data.size());
    }

private:
    DynamicStructuredBuffer(UINT capacity = 64, std::string bufName = "", bool UAV = false)
        : m_capacity(capacity), m_UAV(UAV), m_needsUpdate(false) {
		name = bufName;
        CreateBuffer(capacity);
    }

    void OnSetName() override {
        SetBackingName(m_name, name);
    }

    std::vector<T> m_data;
    uint32_t m_capacity;
    bool m_needsUpdate;

    inline static std::string m_name = "DynamicStructuredBuffer";

    bool m_UAV = false;

    std::vector<EntityComponentBundle> m_metadataBundles;

    void AssignDescriptorSlots(uint32_t capacity)
    {
        BufferBase::DescriptorRequirements requirements{};

        requirements.createCBV = false;
        requirements.createSRV = true;
        requirements.createUAV = m_UAV;
        requirements.createNonShaderVisibleUAV = false;
        requirements.uavCounterOffset = 0;

        // SRV (structured buffer)
        requirements.srvDesc = rhi::SrvDesc{
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
        requirements.uavDesc = rhi::UavDesc{
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

        SetDescriptorRequirements(requirements);
    }


    void CreateBuffer(size_t capacity, size_t previousCapacity = 0) {
        if (m_dataBuffer != nullptr) {
			// If shrinking, copy only up to new capacity. If growing, copy up to previous capacity.
            auto sizeToCopy = capacity < previousCapacity ? capacity : previousCapacity;
            QueueResourceCopyFromOldBacking(sizeToCopy * sizeof(T));
        }
		CreateAndSetBacking(rhi::HeapType::DeviceLocal, sizeof(T) * capacity, m_UAV);
        SetName(name);

        for (const auto& bundle : m_metadataBundles) {
            ApplyMetadataToBacking(bundle);
        }

        AssignDescriptorSlots(static_cast<uint32_t>(capacity));
    }

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) override {
        m_metadataBundles.emplace_back(bundle);
        ApplyMetadataToBacking(bundle);
    }
};