#pragma once

#include <memory>

#include "Resources/ResourceStateTracker.h"
#include "Render/ResourceRegistry.h"

class Resource;

struct ResourceHandleAndRange {
    ResourceHandleAndRange(ResourceRegistry::RegistryHandle resource) : resource(resource) {}
	ResourceHandleAndRange(ResourceRegistry::RegistryHandle resource, const RangeSpec& range) : resource(resource), range(range) {}
    ResourceRegistry::RegistryHandle resource;
    RangeSpec range;
};

struct ResourcePtrAndRange {
	ResourcePtrAndRange(std::shared_ptr<Resource> resource) : resource(resource) {}
	ResourcePtrAndRange(std::shared_ptr<Resource> resource, const RangeSpec& range) : resource(resource), range(range) {}
	std::shared_ptr<Resource> resource;
	RangeSpec range;
};

struct ResourceRequirement {
	ResourceRequirement(const ResourceHandleAndRange& resourceAndRange)
		: resourceHandleAndRange(resourceAndRange) {
	}
	ResourceHandleAndRange resourceHandleAndRange;    // resource and range
    ResourceState state;
};