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

class Buffer : public DynamicBufferBase, public IHasMemoryMetadata {
public:

    static std::shared_ptr<Buffer> CreateShared(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess = false) {
        return std::shared_ptr<Buffer>(new Buffer(accessType, bufferSize, unorderedAccess));
    }

    size_t GetSize() const { return m_bufferSize; }

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) override {
        ApplyMetadataToBacking(bundle);
    }

private:
    Buffer(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess = false)
        : DynamicBufferBase(accessType, bufferSize, unorderedAccess) {
    }

    void OnSetName() override {
        if (!m_dataBuffer) {
            return;
        }
    	m_dataBuffer->SetName(name.c_str());
    }
};