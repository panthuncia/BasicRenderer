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

	ID3D12Resource* GetAPIResource(uint8_t frameIndex) const override {
		spdlog::error("ResourceGroup::GetAPIResource() should never be called, as it is not a single resource.");
		return nullptr;
	}

protected:
    // Override the base Resource method to transition all resources in the group
    std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(uint8_t frameIndex, ResourceState prevState, ResourceState newState) {
		m_transitions.clear();
        for (auto& pair : resourcesByID) {
            auto& trans = pair.second->GetTransitions(frameIndex, prevState, newState);
            for (auto& transition : trans) {
				m_transitions.push_back(transition);
            }
        }

        currentState = newState; // Set the state for the group as a whole
		return m_transitions;
    }
protected:
    std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
    std::vector<D3D12_RESOURCE_BARRIER> m_transitions;
};
