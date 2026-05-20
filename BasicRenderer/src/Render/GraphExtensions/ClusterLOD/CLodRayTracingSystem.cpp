#include "Render/GraphExtensions/ClusterLOD/CLodRayTracingSystem.h"

namespace br::render {

void CLodRayTracingSystem::Refresh(const MeshManager& meshManager) {
    meshManager.GetCLodRayTracingResidencySnapshot(m_snapshot);
    m_pageSources.clear();

    Stats next{};
    next.snapshotGeneration = m_snapshot.pagePoolGeneration;
    next.hasPagePool = m_snapshot.pagePool != nullptr;
    next.residentGroups = static_cast<uint32_t>(m_snapshot.residentGroups.size());

    for (const auto& group : m_snapshot.residentGroups) {
        next.residentPages += static_cast<uint32_t>(group.pageAllocations.size());
        next.meshlets += group.chunk.meshletCount;

        const bool groupHasBuildablePages = !group.pageAllocations.empty();
        const bool groupUsesNativePositions = group.chunk.compressedPositionQuantExp == CLOD_POSITION_FORMAT_FLOAT3;
        if (groupHasBuildablePages && groupUsesNativePositions) {
            ++next.buildableGroups;
        }

        (void)group.segments;

        if (m_snapshot.pagePool) {
            uint32_t localPageIndex = 0;
            for (const PagePool::PageAllocation& allocation : group.pageAllocations) {
                for (uint32_t pageOffset = 0; pageOffset < allocation.pageCount; ++pageOffset) {
                    const uint32_t pageId = allocation.firstPageID + pageOffset;
                    PageSource source{};
                    source.groupGlobalIndex = group.groupGlobalIndex;
                    source.groupLocalPageIndex = localPageIndex++;
                    source.slabIndex = m_snapshot.pagePool->PageToSlabIndex(pageId);
                    source.slabByteOffset = m_snapshot.pagePool->PageToSlabByteOffset(pageId);
                    m_pageSources.push_back(source);
                }
            }
        }
    }

    next.pageSources = static_cast<uint32_t>(m_pageSources.size());
    m_stats = next;
}

void CLodRayTracingSystem::Reset() {
    m_snapshot.residentGroups.clear();
    m_snapshot.pagePool = nullptr;
    m_snapshot.pagePoolGeneration = 0;
    m_pageSources.clear();
    m_stats = {};
}

} // namespace br::render
