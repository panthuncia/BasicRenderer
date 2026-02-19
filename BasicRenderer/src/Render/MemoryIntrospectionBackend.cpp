#include "Render/MemoryIntrospectionBackend.h"

#include "Render/MemoryIntrospectionAPI.h"
#include "Managers/Singletons/ECSManager.h"
#include "Resources/MemoryStatisticsComponents.h"
#include "Resources/ResourceIdentifier.h"

namespace rg::memory {

void AttachSnapshotProviderFromECS() {
    auto& world = ECSManager::GetInstance().GetWorld();
    auto memoryQuery = world.query_builder<const MemoryStatisticsComponents::MemSizeBytes>().build();
    SnapshotProvider::SetBuildSnapshotFn(
        [memoryQuery](std::vector<ResourceMemoryRecord>& out) mutable {
            out.clear();
            out.reserve(2048);

            memoryQuery.each([&](flecs::entity e, const MemoryStatisticsComponents::MemSizeBytes& sz) {
                ResourceMemoryRecord row;
                row.bytes = sz.size;

                if (auto rid = e.try_get<MemoryStatisticsComponents::ResourceID>()) {
                    row.resourceID = rid->id;
                }
                if (auto rt = e.try_get<MemoryStatisticsComponents::ResourceType>()) {
                    row.resourceType = rt->type;
                }
                if (auto rn = e.try_get<MemoryStatisticsComponents::ResourceName>()) {
                    row.resourceName = rn->name;
                }
                if (auto usage = e.try_get<MemoryStatisticsComponents::ResourceUsage>()) {
                    row.usage = usage->usage;
                }
                if (auto ident = e.try_get<ResourceIdentifier>()) {
                    row.identifier = ident->name;
                }

                out.push_back(std::move(row));
                });
        });
}

void DetachSnapshotProvider() {
    SnapshotProvider::ResetBuildSnapshotFn();
}

}
