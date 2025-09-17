#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
#include <functional>
#include <rhi.h>

// per-pass exponential moving average data
struct PassStats {
    double ema = 0.0;
    static constexpr double alpha = 0.1;  // smoothing factor
};

// per-pass mesh shader stats EMA
struct MeshPipelineStats {
    double invocationsEma = 0.0;
    double primitivesEma  = 0.0;
};

class StatisticsManager {
public:
    static StatisticsManager& GetInstance();

    void Initialize();

    void RegisterPasses(const std::vector<std::string>& passNames);
    
    unsigned RegisterPass(const std::string& passName);

    void MarkGeometryPass(const std::string& passName);

    void RegisterQueue(rhi::QueueKind queue);

    void SetupQueryHeap();

    // Timestamp + mesh-stats queries for any pass
    void BeginQuery(unsigned passIndex,
        unsigned frameIndex,
        rhi::Queue& queue,
        rhi::CommandList& cmdList);

    void EndQuery(unsigned passIndex,
        unsigned frameIndex,
        rhi::Queue& queue,
        rhi::CommandList& cmdList);

    // Resolve all queries for a frame before closing
    void ResolveQueries(unsigned frameIndex,
        rhi::Queue& queue,
        rhi::CommandList& cmdList);

    void OnFrameComplete(unsigned frameIndex,
        rhi::Queue& queue);

    void ClearAll();

	const std::vector<bool>& GetIsGeometryPassVector() const { return m_isGeometryPass; }

    const std::vector<std::string>&        GetPassNames() const { return m_passNames; }
    const std::vector<PassStats>&          GetPassStats() const { return m_stats; }
    const std::vector<MeshPipelineStats>&  GetMeshStats() const { return m_meshStatsEma; }

private:
    StatisticsManager() = default;
    ~StatisticsManager() = default;

	bool m_collectPipelineStatistics = false;
	std::function<bool()> m_getCollectPipelineStatistics;

    rhi::QueryPoolPtr m_timestampPool;
    rhi::QueryPoolPtr m_pipelineStatsPool;
	rhi::QueryResultInfo m_timestampQueryInfo;
	rhi::QueryResultInfo m_pipelineStatsQueryInfo;
    std::vector<rhi::PipelineStatsFieldDesc> m_pipelineStatsFields;
	rhi::PipelineStatsLayout m_pipelineStatsLayout;

    std::unordered_map<rhi::QueueKind, rhi::ResourcePtr> m_timestampBuffers;
    std::unordered_map<rhi::QueueKind, rhi::ResourcePtr> m_meshStatsBuffers;

    UINT64    m_gpuTimestampFreq = 0;
    unsigned  m_numPasses = 0;
    unsigned  m_numFramesInFlight = 0;

    // Per-pass data
    std::vector<std::string>        m_passNames;
    std::vector<PassStats>          m_stats;
    std::vector<bool>               m_isGeometryPass;
    std::vector<MeshPipelineStats>  m_meshStatsEma;

    // Recording helpers per queue/frame
    std::unordered_map<rhi::QueueKind,
        std::unordered_map<unsigned, std::vector<unsigned>>> m_recordedQueries;
    std::unordered_map<rhi::QueueKind,
        std::unordered_map<unsigned, std::vector<std::pair<unsigned,unsigned>>>> m_pendingResolves;
};