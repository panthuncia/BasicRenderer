#pragma once

#include <string>
#include <rhi.h>
#include <memory>

#include "Resources/GPUBacking/GpuBufferBacking.h"
#include "Resources/Resource.h"
#include "Resources/Buffers/DynamicBufferBase.h"
#include "Managers/Singletons/UploadManager.h"

using Microsoft::WRL::ComPtr;

class Buffer : public GloballyIndexedResource {
public:

    static std::shared_ptr<Buffer> CreateShared(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess = false) {
        return std::shared_ptr<Buffer>(new Buffer(accessType, bufferSize, unorderedAccess));
    }

    rhi::Resource GetAPIResource() override { return m_dataBuffer->GetAPIResource(); }
    size_t GetSize() const { return m_bufferSize; }

    void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) const {
        m_dataBuffer->ApplyMetadataComponentBundle(bundle);
    }
protected:

    rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) {
        return m_dataBuffer->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
    }

private:
    Buffer(rhi::HeapType accessType, uint64_t bufferSize, bool unorderedAccess = false)
        : m_accessType(accessType), m_bufferSize(bufferSize), m_UAV(unorderedAccess) {
        m_dataBuffer = GpuBufferBacking::CreateUnique(accessType, m_bufferSize, GetGlobalResourceID(), m_UAV);
    }

    void OnSetName() override {
    	m_dataBuffer->SetName(name.c_str());
    }

	rhi::HeapType m_accessType;
	std::unique_ptr<GpuBufferBacking> m_dataBuffer;

    uint64_t m_bufferSize;

    bool m_UAV = false;
};