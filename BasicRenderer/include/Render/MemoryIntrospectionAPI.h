#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Interfaces/IHasMemoryMetadata.h"

namespace rg::memory {

struct ResourceMemoryRecord {
    uint64_t resourceID = 0;
    uint64_t bytes = 0;
    rhi::ResourceType resourceType = rhi::ResourceType::Unknown;
    std::string resourceName;
    std::string usage;
    std::string identifier;
};

class SnapshotProvider {
public:
    using BuildSnapshotFn = std::function<void(std::vector<ResourceMemoryRecord>& out)>;

    static void SetBuildSnapshotFn(BuildSnapshotFn fn);
    static void ResetBuildSnapshotFn();
    static void BuildSnapshot(std::vector<ResourceMemoryRecord>& out);
};

inline void SetResourceUsageHint(IHasMemoryMetadata& resource, std::string usage) {
    resource.SetMemoryUsageHint(std::move(usage));
}

}
