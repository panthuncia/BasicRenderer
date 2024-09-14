#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Resource.h"
#include "GloballyIndexedResource.h"
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

    // Override the base Resource method to transition all resources in the group
    void SetState(ResourceState state) override {
        for (auto& pair : resources) {
            pair.second->SetState(state);
        }
        Resource::SetState(state);  // Set the state for the group as a whole
    }
protected:
    std::unordered_map<uint32_t, std::shared_ptr<GloballyIndexedResource>> resources;
};
