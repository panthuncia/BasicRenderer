#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Resource.h"
#include "GloballyIndexedResource.h"
#include "RenderContext.h"

class ResourceGroup : public Resource {
public:
    ResourceGroup(const std::string& groupName)
        : Resource(groupName) {}


    void AddGloballyIndexedResource(std::shared_ptr<GloballyIndexedResource> resource) {
		resources[resource->GetIndex()] = resource;
    }

    virtual void RemoveGloballyIndexedResource(uint32_t index) {
        auto iter = resources.find(index);
        if (iter != resources.end()) {
            resources.erase(iter);
        }
    }

protected:
    // Override the base Resource method to transition all resources in the group
    void Transition(RenderContext& context, ResourceState prevState, ResourceState newState) {
        for (auto& pair : resources) {
            pair.second->Transition(context, prevState, newState);
        }
        currentState = newState; // Set the state for the group as a whole
    }
protected:
    std::unordered_map<uint32_t, std::shared_ptr<GloballyIndexedResource>> resources;
};
