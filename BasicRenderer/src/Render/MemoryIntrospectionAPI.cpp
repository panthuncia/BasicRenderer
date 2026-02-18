#include "Render/MemoryIntrospectionAPI.h"

#include <mutex>

namespace rg::memory {

namespace {
    std::mutex g_snapshotProviderMutex;
    SnapshotProvider::BuildSnapshotFn g_snapshotBuilder;
}

void SnapshotProvider::SetBuildSnapshotFn(BuildSnapshotFn fn) {
    std::scoped_lock lock(g_snapshotProviderMutex);
    g_snapshotBuilder = std::move(fn);
}

void SnapshotProvider::ResetBuildSnapshotFn() {
    std::scoped_lock lock(g_snapshotProviderMutex);
    g_snapshotBuilder = {};
}

void SnapshotProvider::BuildSnapshot(std::vector<ResourceMemoryRecord>& out) {
    BuildSnapshotFn localBuilder;
    {
        std::scoped_lock lock(g_snapshotProviderMutex);
        localBuilder = g_snapshotBuilder;
    }

    if (!localBuilder) {
        out.clear();
        return;
    }

    localBuilder(out);
}

}
