#pragma once

#include <string>
#include <vector>
#include <directx/d3d12.h>
#include "Resources/ResourceStates.h"

class RenderContext;

struct BarrierGroups {
	unsigned int numBufferBarrierGroups = 0;
	unsigned int numTextureBarrierGroups = 0;
	unsigned int numGlobalBarrierGroups = 0;
    D3D12_BARRIER_GROUP* bufferBarriers = nullptr;
    D3D12_BARRIER_GROUP* textureBarriers = nullptr;
    D3D12_BARRIER_GROUP* globalBarriers = nullptr;
};

class Resource {
public:
    Resource() {
        m_globalResourceID = globalResourceCount.fetch_add(1, std::memory_order_relaxed);
    }


    const std::wstring& GetName() const { return name; }
    virtual void SetName(const std::wstring& name) { this->name = name; OnSetName(); }
	virtual ID3D12Resource* GetAPIResource() const = 0;
    virtual uint64_t GetGlobalResourceID() const { return m_globalResourceID; }
	virtual ResourceAccessType GetCurrentAccessType() const { return m_currentAccessType; }
	virtual ResourceLayout GetCurrentLayout() const { return m_currentLayout; }
	virtual ResourceSyncState GetPrevSyncState() const { return m_prevSyncState; }
    virtual BarrierGroups& GetEnhancedBarrierGroup(ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) = 0;

protected:
    virtual void OnSetName() {}

    ResourceAccessType m_currentAccessType = ResourceAccessType::COMMON;
    ResourceLayout m_currentLayout = ResourceLayout::LAYOUT_COMMON;
    ResourceSyncState m_prevSyncState = ResourceSyncState::ALL;
    std::wstring name;
private:
    bool m_uploadInProgress = false;
    inline static std::atomic<uint64_t> globalResourceCount;
    uint64_t m_globalResourceID;

    //friend class RenderGraph;
    friend class ResourceGroup;
    friend class ResourceManager;
    friend class DynamicResource;
    friend class DynamicGloballyIndexedResource;
    friend class DynamicBuffer;
    friend class UploadManager; // Kinda a hack, for deduplicating transition lists
};