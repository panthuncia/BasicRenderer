#pragma once

#include "Resources/TrackedAllocation.h"

class IHasMemoryMetadata {
	public:
	virtual ~IHasMemoryMetadata() = default;
	virtual void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) = 0;
};