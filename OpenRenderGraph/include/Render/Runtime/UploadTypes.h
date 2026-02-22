#pragma once

#include <memory>

#include "Render/ResourceRegistry.h"

class Resource;

namespace rg::runtime {

struct UploadResolveContext {
    ResourceRegistry* registry = nullptr;
    uint64_t epoch = 0;
};

struct UploadTarget {
    enum class Kind { RegistryHandle, PinnedShared };

    Kind kind{};
    ResourceRegistry::RegistryHandle h{};
    std::shared_ptr<Resource> pinned{};

    static UploadTarget FromHandle(const ResourceRegistry::RegistryHandle& handle) {
        UploadTarget target;
        target.kind = Kind::RegistryHandle;
        target.h = handle;
        return target;
    }

    static UploadTarget FromShared(std::shared_ptr<Resource> resource) {
        UploadTarget target;
        target.kind = Kind::PinnedShared;
        target.pinned = std::move(resource);
        return target;
    }

    bool operator==(const UploadTarget& other) const {
        if (kind != other.kind) {
            return false;
        }

        if (kind == Kind::RegistryHandle) {
            return h.GetKey().idx == other.h.GetKey().idx
                && h.GetGeneration() == other.h.GetGeneration()
                && h.GetEpoch() == other.h.GetEpoch();
        }

        return pinned == other.pinned;
    }
};

}
