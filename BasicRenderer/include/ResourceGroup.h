#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Resource.h"
#include "GloballyIndexedResource.h"
#include "RenderContext.h"

class ResourceGroup : public Resource {
public:
    ResourceGroup(const std::wstring& groupName) {
		name = groupName;
    }

	void AddResource(std::shared_ptr<Resource> resource) {
		resourcesByID[resource->GetGlobalResourceID()] = resource;
	}

	void RemoveResource(Resource* resource) {
		resourcesByID.erase(resource->GetGlobalResourceID());
	}

	ID3D12Resource* GetAPIResource() const override {
		spdlog::error("ResourceGroup::GetAPIResource() should never be called, as it is not a single resource.");
		return nullptr;
	}

protected:
    // Override the base Resource method to transition all resources in the group
    std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState prevState, ResourceState newState) {
		m_transitions.clear();
        for (auto& pair : resourcesByID) {
            auto& trans = pair.second->GetTransitions(prevState, newState);
            for (auto& transition : trans) {
				m_transitions.push_back(transition);
            }
        }
        currentState = newState; // Set the state for the group as a whole
		return m_transitions;
    }

	BarrierGroups& GetEnhancedBarrierGroup(ResourceState prevState, ResourceState newState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState) {
		m_barrierGroups.numBufferBarrierGroups = 0;
		m_barrierGroups.numTextureBarrierGroups = 0;
		m_barrierGroups.numGlobalBarrierGroups = 0;
		m_bufferBarriers.clear();
		m_textureBarriers.clear();
		m_globalBarriers.clear();
		for (auto& pair : resourcesByID) {
			auto& barrierGroup = pair.second->GetEnhancedBarrierGroup(prevState, newState, prevSyncState, newSyncState);
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
		currentState = newState; // Set the state for the group as a whole
		return m_barrierGroups;
	}
protected:
    std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
    std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
	
	// New barriers
	std::vector<D3D12_BARRIER_GROUP> m_bufferBarriers;
	std::vector<D3D12_BARRIER_GROUP> m_textureBarriers;
	std::vector<D3D12_BARRIER_GROUP> m_globalBarriers;
	BarrierGroups m_barrierGroups;
};
