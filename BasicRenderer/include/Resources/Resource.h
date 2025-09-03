#pragma once

#include <string>
#include <vector>

#include <resource_states.h>
#include <rhi.h>

#include "Resources/ResourceStates.h"
#include "Resources/ResourceStateTracker.h"

class RenderContext;

class Resource {
public:
    Resource() {
        m_globalResourceID = globalResourceCount.fetch_add(1, std::memory_order_relaxed);
    }
	virtual ~Resource() = default;


    const std::wstring& GetName() const { return name; }
    virtual void SetName(const std::wstring& newName) { this->name = newName; OnSetName(); }
	virtual rhi::Resource GetAPIResource() const = 0;
    virtual uint64_t GetGlobalResourceID() const { return m_globalResourceID; }
	virtual rhi::ResourceAccessType GetSubresourceAccessType(unsigned int subresourceIndex) const { return m_subresourceAccessTypes[subresourceIndex] ; }
	virtual rhi::ResourceLayout GetSubresourceLayout(unsigned int subresourceIndex) const { return m_subresourceLayouts[subresourceIndex]; }
	virtual rhi::ResourceSyncState GetSubresourceSyncState(unsigned int subresourceIndex) const { return m_subresourceSyncStates[subresourceIndex]; }
    virtual rhi::BarrierBatch GetEnhancedBarrierGroup(RangeSpec range, rhi::ResourceAccessType prevAccessType, rhi::ResourceAccessType newAccessType, rhi::ResourceLayout prevLayout, rhi::ResourceLayout newLayout, rhi::ResourceSyncState prevSyncState, rhi::ResourceSyncState newSyncState) = 0;
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
	std::vector<rhi::ResourceAccessType> m_subresourceAccessTypes;
	std::vector<rhi::ResourceLayout> m_subresourceLayouts;
	std::vector<rhi::ResourceSyncState> m_subresourceSyncStates;
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