#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
#include <functional>

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

    void RegisterQueue(ID3D12CommandQueue* queue);

    void SetupQueryHeap();

    // Timestamp + mesh-stats queries for any pass
    void BeginQuery(unsigned passIndex,
        unsigned frameIndex,
        ID3D12CommandQueue* queue,
        ID3D12GraphicsCommandList* cmdList);
    void EndQuery(unsigned passIndex,
        unsigned frameIndex,
        ID3D12CommandQueue* queue,
        ID3D12GraphicsCommandList* cmdList);

    // Resolve all queries for a frame before closing
    void ResolveQueries(unsigned frameIndex,
        ID3D12CommandQueue* queue,
        ID3D12GraphicsCommandList* cmdList);

    void OnFrameComplete(unsigned frameIndex,
        ID3D12CommandQueue* queue);

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

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_timestampHeap;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_pipelineStatsHeap;

    std::unordered_map<ID3D12CommandQueue*, Microsoft::WRL::ComPtr<ID3D12Resource>> m_timestampBuffers;
    std::unordered_map<ID3D12CommandQueue*, Microsoft::WRL::ComPtr<ID3D12Resource>> m_meshStatsBuffers;

    UINT64    m_gpuTimestampFreq = 0;
    unsigned  m_numPasses = 0;
    unsigned  m_numFramesInFlight = 0;

    // Per-pass data
    std::vector<std::string>        m_passNames;
    std::vector<PassStats>          m_stats;
    std::vector<bool>               m_isGeometryPass;
    std::vector<MeshPipelineStats>  m_meshStatsEma;

    // Recording helpers per queue/frame
    std::unordered_map<ID3D12CommandQueue*,
        std::unordered_map<unsigned, std::vector<unsigned>>> m_recordedQueries;
    std::unordered_map<ID3D12CommandQueue*,
        std::unordered_map<unsigned, std::vector<std::pair<unsigned,unsigned>>>> m_pendingResolves;
};