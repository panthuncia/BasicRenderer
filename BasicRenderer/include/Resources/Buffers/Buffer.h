#pragma once

#include <string>
#include <rhi.h>
#include <memory>

#include "Resources/GPUBacking/GpuBufferBacking.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Managers/Singletons/UploadManager.h"
#include "Interfaces/IHasMemoryMetadata.h"

using Microsoft::WRL::ComPtr;

class Buffer : public BufferBase, public IHasMemoryMetadata {
public:

    static std::shared_ptr<Buffer> CreateShared(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess = false) {
        return std::shared_ptr<Buffer>(new Buffer(accessType, bufferSize, unorderedAccess, true));
    }

    static std::shared_ptr<Buffer> CreateSharedUnmaterialized(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess = false) {
        return std::shared_ptr<Buffer>(new Buffer(accessType, bufferSize, unorderedAccess, false));
    }

    static std::shared_ptr<Buffer> CreateUnmaterializedStructuredBuffer(
        uint32_t numElements,
        uint32_t elementSize,
        bool unorderedAccess,
        bool unorderedAccessCounter = false,
        bool createNonShaderVisibleUAV = false,
        rhi::HeapType accessType = rhi::HeapType::DeviceLocal)
    {
        if (numElements == 0 || elementSize == 0) {
            throw std::runtime_error("Structured buffer requires non-zero element count and element size");
        }

        uint64_t bufferSize = static_cast<uint64_t>(numElements) * static_cast<uint64_t>(elementSize);
        uint64_t counterOffset = 0;

        if (unorderedAccess && unorderedAccessCounter) {
            const uint64_t requiredSize = bufferSize + sizeof(uint32_t);
            const uint64_t alignment = static_cast<uint64_t>(elementSize);
            bufferSize = ((requiredSize + alignment - 1ull) / alignment) * alignment;

            const uint64_t potentialCounterOffset = (requiredSize + 4095ull) & ~4095ull;
            if (potentialCounterOffset + sizeof(uint32_t) <= bufferSize) {
                counterOffset = potentialCounterOffset;
            }
            else {
                bufferSize = ((potentialCounterOffset + sizeof(uint32_t) + alignment - 1ull) / alignment) * alignment;
                counterOffset = potentialCounterOffset;
            }
        }

        auto buffer = CreateSharedUnmaterialized(accessType, bufferSize, unorderedAccess);

        DescriptorRequirements requirements{};
        requirements.createSRV = true;
        requirements.createUAV = unorderedAccess;
        requirements.createNonShaderVisibleUAV = unorderedAccess && createNonShaderVisibleUAV;
        requirements.uavCounterOffset = counterOffset;

        requirements.srvDesc = rhi::SrvDesc{
            .dimension = rhi::SrvDim::Buffer,
            .formatOverride = rhi::Format::Unknown,
            .buffer = {
                .kind = rhi::BufferViewKind::Structured,
                .firstElement = 0,
                .numElements = numElements,
                .structureByteStride = elementSize,
            },
        };

        requirements.uavDesc = rhi::UavDesc{
            .dimension = rhi::UavDim::Buffer,
            .formatOverride = rhi::Format::Unknown,
            .buffer = {
                .kind = rhi::BufferViewKind::Structured,
                .firstElement = 0,
                .numElements = numElements,
                .structureByteStride = elementSize,
                .counterOffsetInBytes = static_cast<uint32_t>(counterOffset),
            },
        };

        buffer->SetDescriptorRequirements(requirements);
        return buffer;
    }

    size_t GetSize() const { return m_bufferSize; }

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) override {
        ApplyMetadataToBacking(bundle);
    }

private:
    Buffer(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess, bool materialize)
        : BufferBase(accessType, bufferSize, unorderedAccess, materialize) {
    }

    void OnSetName() override {
        if (!m_dataBuffer) {
            return;
        }
    	m_dataBuffer->SetName(name.c_str());
    }

    void OnBackingMaterialized() override {
        OnSetName();
    }
};