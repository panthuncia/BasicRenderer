#pragma once

#pragma once

#include <vector>
#include <functional>
#include <typeinfo>
#include <string>

#include "Resources/GPUBacking/GpuBufferBacking.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Resources/Buffers/MemoryBlock.h"

class BufferView;

class DynamicBuffer : public ViewedDynamicBufferBase {
public:

    static std::shared_ptr<DynamicBuffer> CreateShared(size_t elementSize, size_t capacity = 64, std::string name = "", bool byteAddress = false, bool UAV = false) {
        return std::shared_ptr<DynamicBuffer>(new DynamicBuffer(byteAddress, elementSize, capacity, name, UAV));
    }

    std::unique_ptr<BufferView> Allocate(size_t size, size_t elementSize);
    void Deallocate(const BufferView* view);
	std::unique_ptr<BufferView> AddData(const void* data, size_t size, size_t elementSize, size_t fullAllocationSize = 0);
	void UpdateView(BufferView* view, const void* data) override;

    size_t Size() const {
        return m_capacity;
    }

	void* GetMappedData() const {
		return m_mappedData;
	}

	rhi::Resource GetAPIResource() override { return m_dataBuffer->GetAPIResource(); }

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) {
        m_metadataBundles.emplace_back(bundle);
        m_dataBuffer->ApplyMetadataComponentBundle(bundle);
    }

protected:
    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {
        return m_dataBuffer->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
    }

private:
    DynamicBuffer(bool byteAddress, size_t elementSize, size_t capacity, std::string name = "", bool UAV = false)
        : m_byteAddress(byteAddress), m_elementSize(elementSize), m_UAV(UAV), m_needsUpdate(false) {

        size_t bufferSize = elementSize * capacity;
        {
            const size_t align = 4;
            const size_t rem = bufferSize % align;
            if (rem) bufferSize += (align - rem); // Align up to 4 bytes
        }
		m_capacity = bufferSize;
        CreateBuffer(bufferSize);
        SetName(name);
    }

    void OnSetName() override {
        if (name != "") {
			m_name = name;
			std::string newname = m_baseName + ": " + m_name;
            m_dataBuffer->SetName(newname.c_str());
        }
        else {
            m_dataBuffer->SetName(m_baseName.c_str());
        }
    }

    void AssignDescriptorSlots();

	size_t m_elementSize;
	bool m_byteAddress;

    void* m_mappedData = nullptr;

    size_t m_capacity;
    bool m_needsUpdate;

    std::vector<MemoryBlock> m_memoryBlocks;

    inline static std::string m_baseName = "DynamicBuffer";
	std::string m_name = m_baseName;

    bool m_UAV = false;

    std::vector<EntityComponentBundle> m_metadataBundles;

    void CreateBuffer(size_t capacity);
    void GrowBuffer(size_t newSize);
};