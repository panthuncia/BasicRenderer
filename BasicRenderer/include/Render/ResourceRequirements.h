#pragma once

#include <memory>

#include "Resources/ResourceStateTracker.h"
#include "Render/ResourceRegistry.h"

class Resource;

struct ResourceAndRange {
    ResourceAndRange(const std::shared_ptr<Resource>& resource);
	ResourceAndRange(const std::shared_ptr<Resource>& resource, const RangeSpec& range);
    std::shared_ptr<Resource> resource;
    RangeSpec range;
};

struct ResourceRequirement {
	ResourceRequirement(const ResourceAndRange& resourceAndRange)
		: resourceAndRange(resourceAndRange) {
	}
	ResourceAndRange resourceAndRange;    // resource and range
    ResourceState state;
};