#pragma once

#include <vector>
#include <string>
#include <algorithm> // For std::lower_bound, std::upper_bound
#include <rhi.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Interfaces/IHasMemoryMetadata.h"

using Microsoft::WRL::ComPtr;

class SortedUnsignedIntBuffer : public DynamicBufferBase, public IHasMemoryMetadata {
public:
    static std::shared_ptr<SortedUnsignedIntBuffer> CreateShared(uint64_t capacity = 64, std::string name = "", bool UAV = false) {
        return std::shared_ptr<SortedUnsignedIntBuffer>(new SortedUnsignedIntBuffer(capacity, name, UAV));
    }

    // Insert an element while maintaining sorted order (deduped)
    void Insert(unsigned int element);

    // Remove an element (and shift the tail on GPU)
    void Remove(unsigned int element);

    // Get element at index
    unsigned int& operator[](UINT index) {
        return m_data[index];
    }

    const unsigned int& operator[](UINT index) const {
        return m_data[index];
    }

    UINT Size() const {
        return static_cast<UINT>(m_data.size());
    }

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) override {
        m_metadataBundles.emplace_back(bundle);
        ApplyMetadataToBacking(bundle);
    }

private:
    SortedUnsignedIntBuffer(uint64_t capacity = 64, std::string name = "", bool UAV = false)
        : m_capacity(capacity), m_UAV(UAV), m_earliestModifiedIndex(0) {
        CreateBuffer(capacity);
        SetName(name);
    }

    void OnSetName() override {
        if (!m_dataBuffer) {
            return;
        }
        if (name != "") {
            m_dataBuffer->SetName((m_name + ": " + name).c_str());
        }
        else {
            m_dataBuffer->SetName(m_name.c_str());
        }
    }

    void AssignDescriptorSlots();

    // Sorted list of unsigned integers
    std::vector<unsigned int> m_data;

    uint64_t m_capacity;
    uint64_t m_earliestModifiedIndex; // To avoid updating the entire buffer every time

    std::vector<EntityComponentBundle> m_metadataBundles;

    inline static std::string m_name = "SortedUnsignedIntBuffer";

    bool m_UAV = false;

    void CreateBuffer(uint64_t capacity);

    void GrowBuffer(uint64_t newSize);
};
