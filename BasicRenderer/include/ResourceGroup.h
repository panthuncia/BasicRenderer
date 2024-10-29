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

    void AddGloballyIndexedResource(std::shared_ptr<GloballyIndexedResource> resource) {
        resourcesBySRVIndex[resource->GetSRVInfo().index] = resource;
    }

	void AddResource(std::shared_ptr<Resource> resource) {
		resources.push_back(resource);
	}

    virtual void RemoveGloballyIndexedResource(uint32_t index) {
        auto iter = resourcesBySRVIndex.find(index);
        if (iter != resourcesBySRVIndex.end()) {
            resourcesBySRVIndex.erase(iter);
        }
    }

protected:
    // Override the base Resource method to transition all resources in the group
    void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) {
        for (auto& pair : resourcesBySRVIndex) {
            pair.second->Transition(commandList, prevState, newState);
        }
		for (auto& resource : resources) {
			resource->Transition(commandList, prevState, newState);
		}
        currentState = newState; // Set the state for the group as a whole
    }
protected:
    std::unordered_map<uint32_t, std::shared_ptr<Resource>> resourcesBySRVIndex;
	std::vector<std::shared_ptr<Resource>> resources;
};
