#pragma once

#include <string>
#include <vector>

#include <resource_states.h>
#include <rhi.h>
#include <flecs.h>

#include "Resources/ResourceStateTracker.h"
#include "Managers/Singletons/ECSManager.h"

class RenderContext;

class Resource {
public:
    Resource() {
        m_globalResourceID = globalResourceCount.fetch_add(1, std::memory_order_relaxed);
		m_ecsEntity = ECSManager::GetInstance().GetWorld().entity();
    }
	virtual ~Resource() {
		if (!ECSManager::GetInstance().GetWorld()) { // TODO: Hacky way to avoid issues with desctruction order. Is there a better way?
			return;
		}
		if (m_ecsEntity.is_alive()) {
			m_ecsEntity.destruct();
		}
	}


    const std::wstring& GetName() const { return name; }
    virtual void SetName(const std::wstring& newName) { this->name = newName; OnSetName(); }
	virtual rhi::Resource GetAPIResource() = 0;
    virtual uint64_t GetGlobalResourceID() const { return m_globalResourceID; }
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
	flecs::entity& GetECSEntity() {
		return m_ecsEntity;
	}

protected:
    virtual void OnSetName() {}

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
	flecs::entity m_ecsEntity; // For access through ECS queries

    //friend class RenderGraph;
    friend class ResourceGroup;
    friend class ResourceManager;
    friend class DynamicResource;
    friend class DynamicGloballyIndexedResource;
    friend class DynamicBuffer;
    friend class UploadManager; // Kinda a hack, for deduplicating transition lists
};