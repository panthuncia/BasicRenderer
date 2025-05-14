#pragma once

#include <memory>

#include "Resources/ResourceStateTracker.h"

class Resource;

struct ResourceAndRange {
    ResourceAndRange(const std::shared_ptr<Resource>& resource);
    std::shared_ptr<Resource>                   resource;
    RangeSpec                   range;
};

struct ResourceRequirement {
	ResourceRequirement(const ResourceAndRange& resourceAndRange)
		: resourceAndRange(resourceAndRange) {
	}
	ResourceAndRange		  resourceAndRange;    // resource and range
    ResourceState state;
};