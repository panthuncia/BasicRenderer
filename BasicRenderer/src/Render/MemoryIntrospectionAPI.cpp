#include "Render/MemoryIntrospectionAPI.h"

namespace rg::memory {

void SnapshotProvider::SetProvider(std::shared_ptr<IMemorySnapshotProvider> provider) {
    std::scoped_lock lock(m_providerMutex);
    m_provider = std::move(provider);
}

void SnapshotProvider::ResetProvider() {
    std::scoped_lock lock(m_providerMutex);
    m_provider.reset();
}

void SnapshotProvider::BuildSnapshot(std::vector<ResourceMemoryRecord>& out) const {
    std::shared_ptr<IMemorySnapshotProvider> localProvider;
    {
        std::scoped_lock lock(m_providerMutex);
        localProvider = m_provider;
    }

    if (!localProvider) {
        out.clear();
        return;
    }

    localProvider->BuildSnapshot(out);
}

}
