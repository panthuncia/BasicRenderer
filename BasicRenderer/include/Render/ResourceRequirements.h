#pragma once

#include <memory>

#include "Resources/SubresourceView.h"

class Resource;

struct ResourceAndRange {
    ResourceAndRange(const std::shared_ptr<Resource>& resource);
    Resource*                   resource;
    RangeSpec                   range;
};

struct ResourceRequirement {
	ResourceRequirement(const ResourceAndRange& resourceAndRange)
		: resourceAndRange(resourceAndRange) {
	}
	ResourceAndRange		  resourceAndRange;    // resource and range
    ResourceAccessType          access;    // bitmask
    ResourceLayout              layout;
    ResourceSyncState           sync;
};