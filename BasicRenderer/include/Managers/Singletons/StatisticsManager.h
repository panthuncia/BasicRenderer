#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>

// per-pass exponential moving average data
struct PassStats {
    double ema = 0.0;
    static constexpr double alpha = 0.1;  // smoothing factor
};

class StatisticsManager {
public:
    static StatisticsManager& GetInstance();

    void Initialize();

    // Register pass names (order defines pass index)
    void RegisterPasses(const std::vector<std::string>& passNames);

    unsigned int RegisterPass(const std::string& passName);

    void RegisterQueue(ID3D12CommandQueue* queue);

    void SetupQueryHeap();

    void BeginQuery(unsigned passIndex,
        unsigned frameIndex,
        ID3D12CommandQueue* queue,
        ID3D12GraphicsCommandList* cmdList);

    void EndQuery(unsigned passIndex,
        unsigned frameIndex,
        ID3D12CommandQueue* queue,
        ID3D12GraphicsCommandList* cmdList);

    // Call once per batch, after stamping end queries but before closing the command list
    void ResolveQueries(unsigned frameIndex,
        ID3D12CommandQueue* queue,
        ID3D12GraphicsCommandList* cmdList);

    // After the GPU fence for this frameIndex + queue signals, map and update EMAs
    void OnFrameComplete(unsigned frameIndex,
        ID3D12CommandQueue* queue);

    const std::vector<std::string>& GetPassNames() const { return m_passNames; }
    const std::vector<PassStats>&   GetStats()     const { return m_stats; }

    void ClearAll();

private:
    StatisticsManager() = default;
    ~StatisticsManager() = default;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>                              m_queryHeap;
    UINT64                                                               m_gpuTimestampFrequency = 0;
    unsigned                                                             m_numPasses = 0;
    unsigned                                                             m_numFramesInFlight = 0;

    std::vector<std::string>                                             m_passNames;
    std::vector<PassStats>                                               m_stats;

    std::unordered_map<ID3D12CommandQueue*,
        Microsoft::WRL::ComPtr<ID3D12Resource>>                          m_readbackBuffers;

    std::unordered_map<ID3D12CommandQueue*,
        std::unordered_map<unsigned, std::vector<unsigned>>>            m_recordedQueries;

    std::unordered_map<ID3D12CommandQueue*,
        std::unordered_map<unsigned, std::vector<std::pair<unsigned, unsigned>>>>
        m_pendingResolves;
};