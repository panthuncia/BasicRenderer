#pragma once

#include <cstdint>
#include <mutex>
#include <memory>
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

class IMemorySnapshotProvider {
public:
    virtual ~IMemorySnapshotProvider() = default;
    virtual void BuildSnapshot(std::vector<ResourceMemoryRecord>& out) = 0;
};

class SnapshotProvider {
public:
    void SetProvider(std::shared_ptr<IMemorySnapshotProvider> provider);
    void ResetProvider();
    void BuildSnapshot(std::vector<ResourceMemoryRecord>& out) const;

private:
    mutable std::mutex m_providerMutex;
    std::shared_ptr<IMemorySnapshotProvider> m_provider;
};

inline void SetResourceUsageHint(IHasMemoryMetadata& resource, std::string usage) {
    resource.SetMemoryUsageHint(std::move(usage));
}

}
