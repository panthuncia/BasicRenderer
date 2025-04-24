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
    Resource() : currentState(ResourceState::UNKNOWN) {
        m_globalResourceID = globalResourceCount.fetch_add(1, std::memory_order_relaxed);
    }

    virtual ResourceState GetState() const { return currentState; }

    const std::wstring& GetName() const { return name; }
    virtual void SetName(const std::wstring& name) { this->name = name; OnSetName(); }
	virtual ID3D12Resource* GetAPIResource() const = 0;
    uint32_t GetGlobalResourceID() const { return m_globalResourceID; }

protected:
    virtual std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState prevState, ResourceState newState) = 0;
    virtual BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) = 0;
    virtual void OnSetName() {}
    virtual void SetState(ResourceState state) { currentState = state; }

    ResourceState currentState;
	ResourceSyncState currentSyncState = ResourceSyncState::NONE;
    std::wstring name;
private:
    bool m_uploadInProgress = false;
    inline static std::atomic<uint32_t> globalResourceCount;
    uint32_t m_globalResourceID;

    friend class RenderGraph;
    friend class ResourceGroup;
    friend class ResourceManager;
    friend class DynamicResource;
    friend class DynamicGloballyIndexedResource;
    friend class DynamicBuffer;
    friend class UploadManager; // Kinda a hack, for deduplicating transition lists
};