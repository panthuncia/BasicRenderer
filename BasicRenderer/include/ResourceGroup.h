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
        resourcesByIndex[resource->GetIndex()] = resource;
    }

	void AddResource(std::shared_ptr<Resource> resource) {
		resources.push_back(resource);
	}

    virtual void RemoveGloballyIndexedResource(uint32_t index) {
        auto iter = resourcesByIndex.find(index);
        if (iter != resourcesByIndex.end()) {
            resourcesByIndex.erase(iter);
        }
    }

protected:
    // Override the base Resource method to transition all resources in the group
    void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) {
        for (auto& pair : resourcesByIndex) {
            pair.second->Transition(commandList, prevState, newState);
        }
		for (auto& resource : resources) {
			resource->Transition(commandList, prevState, newState);
		}
        currentState = newState; // Set the state for the group as a whole
    }
protected:
    std::unordered_map<uint32_t, std::shared_ptr<Resource>> resourcesByIndex;
	std::vector<std::shared_ptr<Resource>> resources;
};
