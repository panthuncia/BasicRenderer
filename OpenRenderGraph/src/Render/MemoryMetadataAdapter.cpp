#include "Interfaces/IHasMemoryMetadata.h"

#include "Resources/MemoryStatisticsComponents.h"

void IHasMemoryMetadata::SetMemoryUsageHint(std::string usage) {
    EntityComponentBundle bundle;
    bundle.Set<MemoryStatisticsComponents::ResourceUsage>({ std::move(usage) });
    ApplyMetadataComponentBundle(bundle);
}
