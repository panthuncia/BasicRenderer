#pragma once

#include <cstdint>
#include <vector>

#include "Managers/MeshManager.h"

namespace br::render {

class CLodRayTracingSystem {
public:
    struct Stats {
        uint32_t residentGroups = 0;
        uint32_t residentPages = 0;
        uint32_t pageSources = 0;
        uint32_t buildableGroups = 0;
        uint32_t meshlets = 0;
        uint32_t triangles = 0;
        uint64_t snapshotGeneration = 0;
        bool hasPagePool = false;
    };

    struct PageSource {
        uint32_t groupGlobalIndex = 0;
        uint32_t groupLocalPageIndex = 0;
        uint32_t slabIndex = 0;
        uint64_t slabByteOffset = 0;
    };

    void Refresh(const MeshManager& meshManager);
    void Reset();

    const Stats& GetStats() const { return m_stats; }
    const MeshManager::CLodRayTracingResidencySnapshot& GetSnapshot() const { return m_snapshot; }
    const std::vector<PageSource>& GetPageSources() const { return m_pageSources; }
    bool HasBuildableGeometry() const { return m_stats.buildableGroups > 0; }

private:
    MeshManager::CLodRayTracingResidencySnapshot m_snapshot;
    std::vector<PageSource> m_pageSources;
    Stats m_stats{};
};

} // namespace br::render
