#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <rhi.h>

#include "Render/Runtime/StatisticsTypes.h"

namespace rg::runtime {

class IStatisticsService {
public:
    virtual ~IStatisticsService() = default;

    virtual void Initialize() = 0;
    virtual void BeginFrame() = 0;
    virtual void ClearAll() = 0;

    virtual unsigned RegisterPass(const std::string& passName, bool isGeometryPass) = 0;
    virtual void RegisterQueue(rhi::QueueKind queueKind) = 0;
    virtual void SetupQueryHeap() = 0;

    virtual void BeginQuery(unsigned passIndex, unsigned frameIndex, rhi::Queue& queue, rhi::CommandList& cmdList) = 0;
    virtual void EndQuery(unsigned passIndex, unsigned frameIndex, rhi::Queue& queue, rhi::CommandList& cmdList) = 0;
    virtual void ResolveQueries(unsigned frameIndex, rhi::Queue& queue, rhi::CommandList& cmdList) = 0;
    virtual void OnFrameComplete(unsigned frameIndex, rhi::Queue& queue) = 0;

    virtual const std::vector<std::string>& GetPassNames() const = 0;
    virtual const std::vector<PassStats>& GetPassStats() const = 0;
    virtual const std::vector<MeshPipelineStats>& GetMeshStats() const = 0;
    virtual MemoryBudgetStats GetMemoryBudgetStats() const = 0;
    virtual const std::vector<bool>& GetIsGeometryPassVector() const = 0;
    virtual const std::vector<unsigned>& GetVisiblePassIndices(uint64_t maxStaleFrames) const = 0;
};

std::shared_ptr<IStatisticsService> CreateDefaultStatisticsService();

}
