#pragma once

#include <string>
#include <rhi.h>
#include <rhi_helpers.h>
#include <memory>
#include <limits>
#include <vector>

#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Interfaces/IHasMemoryMetadata.h"

using Microsoft::WRL::ComPtr;

class Buffer : public BufferBase, public IHasMemoryMetadata {
public:

    struct StructuredBufferParams {
        uint32_t numElements = 0;
        uint32_t elementSize = 0;
        bool unorderedAccessCounter = false;
        bool createNonShaderVisibleUAV = false;
    };

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

        const StructuredLayout layout = ComputeStructuredLayout(numElements, elementSize, unorderedAccess, unorderedAccessCounter);
        uint64_t bufferSize = layout.bufferSize;
        uint64_t counterOffset = layout.counterOffset;

        auto buffer = CreateSharedUnmaterialized(accessType, bufferSize, unorderedAccess);

        buffer->m_structuredParams = StructuredBufferParams{
            .numElements = numElements,
            .elementSize = elementSize,
            .unorderedAccessCounter = unorderedAccessCounter,
            .createNonShaderVisibleUAV = createNonShaderVisibleUAV,
        };

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

    // TODO: Should these expose the ability to copy the old buffer's contents into the new buffer if materialized?
    bool ResizeBytes(uint64_t newBufferSize) {
        if (newBufferSize == 0) {
            throw std::runtime_error("Cannot resize buffer to zero bytes");
        }
        if (m_structuredParams.has_value()) {
            throw std::runtime_error("Use ResizeStructured for structured buffers");
        }
        if (newBufferSize == m_bufferSize) {
            return false;
        }

        const bool wasMaterialized = IsMaterialized();
        if (wasMaterialized) {
            Dematerialize();
        }

        UpdateDescriptorsForByteSize(newBufferSize);
        ConfigureBacking(m_accessType, newBufferSize, m_unorderedAccess);

        if (wasMaterialized) {
            Materialize();
        }
        return true;
    }

    bool ResizeStructured(uint32_t newNumElements) {
        if (!m_structuredParams.has_value()) {
            throw std::runtime_error("ResizeStructured called on a non-structured buffer");
        }
        if (newNumElements == 0) {
            throw std::runtime_error("Structured buffer resize requires non-zero element count");
        }
        if (newNumElements == m_structuredParams->numElements) {
            return false;
        }

        StructuredBufferParams params = *m_structuredParams;
        params.numElements = newNumElements;

        const StructuredLayout layout = ComputeStructuredLayout(
            params.numElements,
            params.elementSize,
            m_unorderedAccess,
            params.unorderedAccessCounter);

        const bool wasMaterialized = IsMaterialized();
        if (wasMaterialized) {
            Dematerialize();
        }

        UpdateDescriptorsForStructuredResize(params, layout.counterOffset);
        m_structuredParams = params;
        ConfigureBacking(m_accessType, layout.bufferSize, m_unorderedAccess);

        if (wasMaterialized) {
            Materialize();
        }
        return true;
    }

private:
    struct StructuredLayout {
        uint64_t bufferSize = 0;
        uint64_t counterOffset = 0;
    };

    Buffer(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess, bool materialize)
        : BufferBase(accessType, bufferSize, unorderedAccess, materialize) {
    }

    static uint32_t ClampToUint32(uint64_t value, const char* what) {
        if (value > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
            throw std::runtime_error(std::string("Buffer resize exceeds uint32 range for ") + what);
        }
        return static_cast<uint32_t>(value);
    }

    static StructuredLayout ComputeStructuredLayout(
        uint32_t numElements,
        uint32_t elementSize,
        bool unorderedAccess,
        bool unorderedAccessCounter)
    {
        StructuredLayout out{};
        out.bufferSize = static_cast<uint64_t>(numElements) * static_cast<uint64_t>(elementSize);

        if (unorderedAccess && unorderedAccessCounter) {
            const uint64_t requiredSize = out.bufferSize + sizeof(uint32_t);
            const uint64_t alignment = static_cast<uint64_t>(elementSize);
            out.bufferSize = ((requiredSize + alignment - 1ull) / alignment) * alignment;

            const uint64_t potentialCounterOffset = (requiredSize + 4095ull) & ~4095ull;
            if (potentialCounterOffset + sizeof(uint32_t) <= out.bufferSize) {
                out.counterOffset = potentialCounterOffset;
            }
            else {
                out.bufferSize = ((potentialCounterOffset + sizeof(uint32_t) + alignment - 1ull) / alignment) * alignment;
                out.counterOffset = potentialCounterOffset;
            }
        }

        return out;
    }

    void UpdateDescriptorsForStructuredResize(const StructuredBufferParams& params, uint64_t counterOffset) {
        if (!m_descriptorRequirements.has_value()) {
            return;
        }

        auto requirements = *m_descriptorRequirements;
        requirements.uavCounterOffset = counterOffset;

        if (requirements.createSRV && requirements.srvDesc.dimension == rhi::SrvDim::Buffer) {
            requirements.srvDesc.buffer.numElements = params.numElements;
            requirements.srvDesc.buffer.structureByteStride = params.elementSize;
        }

        if (requirements.createUAV && requirements.uavDesc.dimension == rhi::UavDim::Buffer) {
            requirements.uavDesc.buffer.numElements = params.numElements;
            requirements.uavDesc.buffer.structureByteStride = params.elementSize;
            requirements.uavDesc.buffer.counterOffsetInBytes = ClampToUint32(counterOffset, "UAV counter offset");
        }

        m_descriptorRequirements = requirements;
    }

    void UpdateDescriptorsForByteSize(uint64_t newBufferSize) {
        if (!m_descriptorRequirements.has_value()) {
            return;
        }

        auto requirements = *m_descriptorRequirements;

        if (requirements.createCBV) {
            requirements.cbvDesc.byteSize = ClampToUint32(newBufferSize, "CBV byte size");
        }

        auto updateSrvNumElements = [&](rhi::SrvDesc& desc) {
            if (desc.dimension != rhi::SrvDim::Buffer) {
                return;
            }

            switch (desc.buffer.kind) {
            case rhi::BufferViewKind::Raw:
                desc.buffer.numElements = ClampToUint32(newBufferSize / 4ull, "SRV raw element count");
                break;
            case rhi::BufferViewKind::Structured:
                if (desc.buffer.structureByteStride == 0) {
                    throw std::runtime_error("Structured SRV resize requires non-zero stride");
                }
                desc.buffer.numElements = ClampToUint32(newBufferSize / desc.buffer.structureByteStride, "SRV structured element count");
                break;
            case rhi::BufferViewKind::Typed: {
                const uint32_t elementSize = static_cast<uint32_t>(rhi::helpers::BytesPerBlock(desc.formatOverride));
                if (elementSize == 0) {
                    throw std::runtime_error("Typed SRV resize requires a valid format");
                }
                desc.buffer.numElements = ClampToUint32(newBufferSize / elementSize, "SRV typed element count");
                break;
            }
            }
        };

        auto updateUavNumElements = [&](rhi::UavDesc& desc) {
            if (desc.dimension != rhi::UavDim::Buffer) {
                return;
            }

            switch (desc.buffer.kind) {
            case rhi::BufferViewKind::Raw:
                desc.buffer.numElements = ClampToUint32(newBufferSize / 4ull, "UAV raw element count");
                break;
            case rhi::BufferViewKind::Structured:
                if (desc.buffer.structureByteStride == 0) {
                    throw std::runtime_error("Structured UAV resize requires non-zero stride");
                }
                desc.buffer.numElements = ClampToUint32(newBufferSize / desc.buffer.structureByteStride, "UAV structured element count");
                break;
            case rhi::BufferViewKind::Typed: {
                const uint32_t elementSize = static_cast<uint32_t>(rhi::helpers::BytesPerBlock(desc.formatOverride));
                if (elementSize == 0) {
                    throw std::runtime_error("Typed UAV resize requires a valid format");
                }
                desc.buffer.numElements = ClampToUint32(newBufferSize / elementSize, "UAV typed element count");
                break;
            }
            }
        };

        if (requirements.createSRV) {
            updateSrvNumElements(requirements.srvDesc);
        }
        if (requirements.createUAV) {
            updateUavNumElements(requirements.uavDesc);
        }

        m_descriptorRequirements = requirements;
    }

    void OnSetName() override;

    void OnBackingMaterialized() override {
        for (const auto& bundle : m_metadataBundles) {
            ApplyMetadataToBacking(bundle);
        }
        OnSetName();
    }

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) override {
        m_metadataBundles.emplace_back(bundle);
        ApplyMetadataToBacking(bundle);
    }

    std::optional<StructuredBufferParams> m_structuredParams;
    std::vector<EntityComponentBundle> m_metadataBundles;
};