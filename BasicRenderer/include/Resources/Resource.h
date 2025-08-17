#pragma once

#include <string>
#include <vector>
#include <directx/d3d12.h>
#include "Resources/ResourceStates.h"
#include "Resources/ResourceStateTracker.h"

class RenderContext;

struct BarrierGroups {
	std::vector<D3D12_TEXTURE_BARRIER>   textureBarrierDescs;
	std::vector<D3D12_BUFFER_BARRIER>    bufferBarrierDescs;
	std::vector<D3D12_GLOBAL_BARRIER>    globalBarrierDescs;

    std::vector<D3D12_BARRIER_GROUP> bufferBarriers;
    std::vector<D3D12_BARRIER_GROUP> textureBarriers;
    std::vector<D3D12_BARRIER_GROUP> globalBarriers;

	BarrierGroups() = default; // Default constructor
	BarrierGroups(const BarrierGroups&) = delete;            // no copy, since the ptrs in D3D12_BARRIER_GROUP will be invalid
	BarrierGroups& operator=(const BarrierGroups&) = delete;
	BarrierGroups(BarrierGroups&&) = default;                // allow move
	BarrierGroups& operator=(BarrierGroups&&) = default;
};

class Resource {
public:
    Resource() {
        m_globalResourceID = globalResourceCount.fetch_add(1, std::memory_order_relaxed);
    }
	virtual ~Resource() = default;


    const std::wstring& GetName() const { return name; }
    virtual void SetName(const std::wstring& newName) { this->name = newName; OnSetName(); }
	virtual ID3D12Resource* GetAPIResource() const = 0;
    virtual uint64_t GetGlobalResourceID() const { return m_globalResourceID; }
	virtual ResourceAccessType GetSubresourceAccessType(unsigned int subresourceIndex) const { return m_subresourceAccessTypes[subresourceIndex] ; }
	virtual ResourceLayout GetSubresourceLayout(unsigned int subresourceIndex) const { return m_subresourceLayouts[subresourceIndex]; }
	virtual ResourceSyncState GetSubresourceSyncState(unsigned int subresourceIndex) const { return m_subresourceSyncStates[subresourceIndex]; }
    virtual BarrierGroups GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) = 0;
	bool HasLayout() const { return m_hasLayout; }
	void AddAliasedResource(Resource* resource) {
		m_aliasedResources.push_back(resource);
	}
	bool HasAliasedResources() const {
		return !m_aliasedResources.empty();
	}
	std::vector<Resource*> GetAliasedResources() const {
		return m_aliasedResources;
	}
	unsigned int GetMipLevels() const { return m_mipLevels; }
	unsigned int GetArraySize() const { return m_arraySize; }
	std::pair<unsigned int, unsigned int> GetSubresourceMipSlice(unsigned int subresourceIndex) const {
		unsigned int mip = subresourceIndex % m_mipLevels;
		unsigned int slice = subresourceIndex / m_mipLevels;
		return std::make_pair(mip, slice);
	}

	virtual SymbolicTracker* GetStateTracker() {
		return &m_stateTracker;
	}

protected:
    virtual void OnSetName() {}

    //ResourceAccessType m_currentAccessType = ResourceAccessType::COMMON;
    //ResourceLayout m_currentLayout = ResourceLayout::LAYOUT_COMMON;
    //ResourceSyncState m_prevSyncState = ResourceSyncState::ALL;
	std::vector<ResourceAccessType> m_subresourceAccessTypes;
	std::vector<ResourceLayout> m_subresourceLayouts;
	std::vector<ResourceSyncState> m_subresourceSyncStates;
    std::wstring name;
	bool m_hasLayout = false; // Only textures have a layout
	std::vector<Resource*> m_aliasedResources; // Resources that are aliased with this resource

    unsigned int m_mipLevels = 1;
	unsigned int m_arraySize = 1;

private:
    bool m_uploadInProgress = false;
    inline static std::atomic<uint64_t> globalResourceCount;
    uint64_t m_globalResourceID;
	SymbolicTracker m_stateTracker;

    //friend class RenderGraph;
    friend class ResourceGroup;
    friend class ResourceManager;
    friend class DynamicResource;
    friend class DynamicGloballyIndexedResource;
    friend class DynamicBuffer;
    friend class UploadManager; // Kinda a hack, for deduplicating transition lists
};