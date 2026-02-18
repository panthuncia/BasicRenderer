#pragma once

#include <string>

#include "Resources/TrackedAllocation.h"
#include "Resources/MemoryStatisticsComponents.h"

class IHasMemoryMetadata {
	public:
	virtual ~IHasMemoryMetadata() = default;
	virtual void SetMemoryUsageHint(std::string usage) {
		EntityComponentBundle bundle;
		bundle.Set<MemoryStatisticsComponents::ResourceUsage>({ std::move(usage) });
		ApplyMetadataComponentBundle(bundle);
	}
private:
	virtual void ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) = 0;
	friend class RenderGraph;
};