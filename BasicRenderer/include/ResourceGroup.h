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

	void AddIndexedResource(std::shared_ptr<Resource> resource, uint64_t index) { // uint64_t to allow Cantor pairing
        resourcesByID[index] = resource;
    }

	void AddResource(std::shared_ptr<Resource> resource) {
		resources.push_back(resource);
	}

    virtual void RemoveIndexedResource(uint32_t index) {
        auto iter = resourcesByID.find(index);
        if (iter != resourcesByID.end()) {
            resourcesByID.erase(iter);
        }
    }

	ID3D12Resource* GetAPIResource() const override {
		spdlog::error("ResourceGroup::GetAPIResource() should never be called, as it is not a single resource.");
		return nullptr;
	}

protected:
    // Override the base Resource method to transition all resources in the group
    void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) {
        for (auto& pair : resourcesByID) {
            pair.second->Transition(commandList, prevState, newState);
        }
		for (auto& resource : resources) {
			resource->Transition(commandList, prevState, newState);
		}
        currentState = newState; // Set the state for the group as a whole
    }
protected:
    std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
	std::vector<std::shared_ptr<Resource>> resources;
};
