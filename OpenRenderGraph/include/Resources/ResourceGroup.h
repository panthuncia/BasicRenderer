#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <rhi.h>

#include "Resources/Resource.h"
#include "Resources/GloballyIndexedResource.h"
#include "Render/RenderContext.h"

class ResourceGroup {
public:
    ResourceGroup(const std::string& groupName) : name(groupName) {
    }

	const std::vector<std::shared_ptr<Resource>>& GetChildren() const {
		return resources;
	}

	void AddResource(const std::shared_ptr<Resource>& resource) {
		auto id = resource->GetGlobalResourceID();
		if (!resourcesByID.contains(id)) {
			resourcesByID[resource->GetGlobalResourceID()] = resource;
			resources.push_back(resource);
		}
	}

	void RemoveResource(const Resource* resource) {
		const auto id = resource->GetGlobalResourceID();
		auto it = resourcesByID.find(id);
		if (it != resourcesByID.end()) {
			const auto& sp = it->second;
			resources.erase(std::remove(resources.begin(), resources.end(), sp), resources.end());
			resourcesByID.erase(it);
		}
	}

	void ClearResources() {
		resources.clear();
		resourcesByID.clear();
	}

protected:


    std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
	std::vector<std::shared_ptr<Resource>> resources;

	std::string name = "";

private:

	std::vector<uint64_t> GetChildIDs() const {
		std::vector<uint64_t> children;
		for (auto& resource : resources) {
			children.push_back(resource->GetGlobalResourceID());
		}
		return children;
	}

};
