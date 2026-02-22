#include "Render/Runtime/IStatisticsService.h"

#include "Managers/Singletons/StatisticsManager.h"

namespace rg::runtime {

namespace {
class DefaultStatisticsService final : public IStatisticsService {
public:
    void Initialize() override {
        StatisticsManager::GetInstance().Initialize();
    }

    void BeginFrame() override {
        StatisticsManager::GetInstance().BeginFrame();
    }

    void ClearAll() override {
        StatisticsManager::GetInstance().ClearAll();
    }

    unsigned RegisterPass(const std::string& passName, bool isGeometryPass) override {
        return StatisticsManager::GetInstance().RegisterPass(passName, isGeometryPass);
    }

    void RegisterQueue(rhi::QueueKind queueKind) override {
        StatisticsManager::GetInstance().RegisterQueue(queueKind);
    }

    void SetupQueryHeap() override {
        StatisticsManager::GetInstance().SetupQueryHeap();
    }

    void BeginQuery(unsigned passIndex, unsigned frameIndex, rhi::Queue& queue, rhi::CommandList& cmdList) override {
        StatisticsManager::GetInstance().BeginQuery(passIndex, frameIndex, queue, cmdList);
    }

    void EndQuery(unsigned passIndex, unsigned frameIndex, rhi::Queue& queue, rhi::CommandList& cmdList) override {
        StatisticsManager::GetInstance().EndQuery(passIndex, frameIndex, queue, cmdList);
    }

    void ResolveQueries(unsigned frameIndex, rhi::Queue& queue, rhi::CommandList& cmdList) override {
        StatisticsManager::GetInstance().ResolveQueries(frameIndex, queue, cmdList);
    }

    void OnFrameComplete(unsigned frameIndex, rhi::Queue& queue) override {
        StatisticsManager::GetInstance().OnFrameComplete(frameIndex, queue);
    }

    const std::vector<std::string>& GetPassNames() const override {
        return StatisticsManager::GetInstance().GetPassNames();
    }

    const std::vector<PassStats>& GetPassStats() const override {
        return StatisticsManager::GetInstance().GetPassStats();
    }

    const std::vector<MeshPipelineStats>& GetMeshStats() const override {
        return StatisticsManager::GetInstance().GetMeshStats();
    }

    MemoryBudgetStats GetMemoryBudgetStats() const override {
        return StatisticsManager::GetInstance().GetMemoryBudgetStats();
    }

    const std::vector<bool>& GetIsGeometryPassVector() const override {
        return StatisticsManager::GetInstance().GetIsGeometryPassVector();
    }

    const std::vector<unsigned>& GetVisiblePassIndices(uint64_t maxStaleFrames) const override {
        return StatisticsManager::GetInstance().GetVisiblePassIndices(maxStaleFrames);
    }
};
}

std::shared_ptr<IStatisticsService> CreateDefaultStatisticsService() {
    return std::make_shared<DefaultStatisticsService>();
}

}
