#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Resources/Resource.h"
#include "Resources/GloballyIndexedResource.h"
#include "Render/RenderContext.h"

class ResourceGroup : public Resource {
public:
    ResourceGroup(const std::wstring& groupName) {
		name = groupName;
    }

	void AddResource(std::shared_ptr<Resource> resource) {
		auto id = resource->GetGlobalResourceID();
		if (!resourcesByID.contains(id)) {
			resourcesByID[resource->GetGlobalResourceID()] = resource;
			resources.push_back(resource);
		}
	}

	void RemoveResource(Resource* resource) {
		resources.erase(std::remove(resources.begin(), resources.end(), resourcesByID[resource->GetGlobalResourceID()]), resources.end());
		resourcesByID.erase(resource->GetGlobalResourceID());
	}

	void ClearResources() {
		resourcesByID.clear();
	}

	ID3D12Resource* GetAPIResource() const override {
		spdlog::error("ResourceGroup::GetAPIResource() should never be called, as it is not a single resource.");
		return nullptr;
	}

protected:

	BarrierGroups& GetEnhancedBarrierGroup(RangeSpec range, ResourceAccessType prevAccessType, ResourceAccessType newAccessType, ResourceLayout prevLayout, ResourceLayout newLayout, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
		m_barrierGroups.numBufferBarrierGroups = 0;
		m_barrierGroups.numTextureBarrierGroups = 0;
		m_barrierGroups.numGlobalBarrierGroups = 0;
		m_bufferBarriers.clear();
		m_textureBarriers.clear();
		m_globalBarriers.clear();
		for (auto& resource : standardTransitionResources) {
			auto& barrierGroup = resource->GetEnhancedBarrierGroup(range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState);
			if (barrierGroup.numBufferBarrierGroups > 0) {
				m_bufferBarriers.insert(m_bufferBarriers.end(), barrierGroup.bufferBarriers, barrierGroup.bufferBarriers + barrierGroup.numBufferBarrierGroups);
				m_barrierGroups.numBufferBarrierGroups += barrierGroup.numBufferBarrierGroups;
			}
			if (barrierGroup.numTextureBarrierGroups > 0) {
				m_textureBarriers.insert(m_textureBarriers.end(), barrierGroup.textureBarriers, barrierGroup.textureBarriers + barrierGroup.numTextureBarrierGroups);
				m_barrierGroups.numTextureBarrierGroups += barrierGroup.numTextureBarrierGroups;
			}
			if (barrierGroup.numGlobalBarrierGroups > 0) {
				m_globalBarriers.insert(m_globalBarriers.end(), barrierGroup.globalBarriers, barrierGroup.globalBarriers + barrierGroup.numGlobalBarrierGroups);
				m_barrierGroups.numGlobalBarrierGroups += barrierGroup.numGlobalBarrierGroups;
			}
		}
		m_barrierGroups.bufferBarriers = m_bufferBarriers.data();
		m_barrierGroups.textureBarriers = m_textureBarriers.data();
		m_barrierGroups.globalBarriers = m_globalBarriers.data();
		return m_barrierGroups;
	}

    std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
	std::vector<std::shared_ptr<Resource>> resources;
	std::vector<std::shared_ptr<Resource>> standardTransitionResources;
    std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
	
	// New barriers
	std::vector<D3D12_BARRIER_GROUP> m_bufferBarriers;
	std::vector<D3D12_BARRIER_GROUP> m_textureBarriers;
	std::vector<D3D12_BARRIER_GROUP> m_globalBarriers;

	BarrierGroups m_barrierGroups;

private:

	void InitializeForGraph() {
		standardTransitionResources.clear();
		for (auto& resource : resources) {
			standardTransitionResources.push_back(resource);
		}
	}

	std::vector<uint64_t> GetChildIDs() {
		std::vector<uint64_t> children;
		for (auto& resource : resources) {
			auto resourceGroup = std::dynamic_pointer_cast<ResourceGroup>(resource);
			if (resourceGroup) {
				auto grandchildren = resourceGroup->GetChildIDs();
				for (auto grandchild : grandchildren) {
					children.push_back(grandchild);
				}
			}
			children.push_back(resource->GetGlobalResourceID());
		}
		return children;
	}

	void MarkResourceAsNonStandard(std::shared_ptr<Resource> resource) {
		auto it = std::find(standardTransitionResources.begin(), standardTransitionResources.end(), resource);
		if (it != standardTransitionResources.end()) {
			standardTransitionResources.erase(it);
		}
	}

	friend class RenderGraph;
};
