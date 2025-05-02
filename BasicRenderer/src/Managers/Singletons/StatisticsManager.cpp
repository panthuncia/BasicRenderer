#include "Managers/Singletons/StatisticsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include <DirectX/d3dx12.h>
#include <algorithm>

StatisticsManager& StatisticsManager::GetInstance() {
    static StatisticsManager instance;
    return instance;
}

void StatisticsManager::Initialize() {
    // Cache frames-in-flight count and GPU frequency
    m_numFramesInFlight = SettingsManager::GetInstance()
        .getSettingGetter<uint8_t>("numFramesInFlight")();
    auto queue = DeviceManager::GetInstance().GetGraphicsQueue();
    queue->GetTimestampFrequency(&m_gpuTimestampFrequency);
}

void StatisticsManager::RegisterPasses(const std::vector<std::string>& passNames) {
    m_passNames = passNames;
    m_numPasses = static_cast<unsigned>(passNames.size());
    m_stats.assign(m_numPasses, PassStats());
}

unsigned int StatisticsManager::RegisterPass(const std::string& passName) {
	m_passNames.push_back(passName);
	m_numPasses++;
	m_stats.emplace_back();

	return m_numPasses - 1;
}

void StatisticsManager::RegisterQueue(ID3D12CommandQueue* queue) {
    // Prepare structures for this queue
    m_readbackBuffers[queue];
    m_recordedQueries[queue];
    m_pendingResolves[queue];
}

void StatisticsManager::SetupQueryHeap() {
    auto& device = DeviceManager::GetInstance().GetDevice();

    // Create the timestamp query heap
    D3D12_QUERY_HEAP_DESC qhd = {};
    qhd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = m_numPasses * 2 * m_numFramesInFlight;
    device->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_queryHeap));

    // Allocate a readback buffer for each registered queue
    UINT64 bufSize = sizeof(UINT64) * qhd.Count;
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
    auto props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    for (auto& kv : m_readbackBuffers) {
        device->CreateCommittedResource(
            &props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&kv.second)
        );
    }
}

void StatisticsManager::BeginQuery(unsigned passIndex,
    unsigned frameIndex,
    ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* cmdList) {
	if (passIndex < 0 || passIndex >= m_numPasses) {
        return;
	}
    unsigned queryIndex = (frameIndex * m_numPasses + passIndex) * 2 + 0;
    cmdList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
    m_recordedQueries[queue][frameIndex].push_back(queryIndex);
}

void StatisticsManager::EndQuery(unsigned passIndex,
    unsigned frameIndex,
    ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* cmdList) {
    if (passIndex < 0 || passIndex >= m_numPasses) {
        return;
    }
    unsigned queryIndex = (frameIndex * m_numPasses + passIndex) * 2 + 1;
    cmdList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
    m_recordedQueries[queue][frameIndex].push_back(queryIndex);
}

void StatisticsManager::ResolveQueries(unsigned frameIndex,
    ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* cmdList) {
    auto& rec = m_recordedQueries[queue][frameIndex];
    if (rec.empty()) return;

    std::sort(rec.begin(), rec.end());
    std::vector<std::pair<unsigned,unsigned>> ranges;
    unsigned start = rec[0], prev = rec[0];
    for (size_t i = 1; i < rec.size(); ++i) {
        if (rec[i] == prev + 1) {
            prev = rec[i];
        } else {
            ranges.emplace_back(start, prev - start + 1);
            start = rec[i]; prev = rec[i];
        }
    }
    ranges.emplace_back(start, prev - start + 1);

    // Issue ResolveQueryData for each contiguous block
    for (auto& r : ranges) {
        cmdList->ResolveQueryData(
            m_queryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            r.first,
            r.second,
            m_readbackBuffers[queue].Get(),
            sizeof(UINT64) * r.first
        );
        m_pendingResolves[queue][frameIndex].push_back(r);
    }
    rec.clear();
}

void StatisticsManager::OnFrameComplete(unsigned frameIndex,
    ID3D12CommandQueue* queue) {
    auto& pending = m_pendingResolves[queue][frameIndex];
    if (pending.empty()) return;

    auto& buf = m_readbackBuffers[queue];
    for (auto& r : pending) {
        // Map only the subrange we just resolved
        D3D12_RANGE readRange{ r.first * sizeof(UINT64), (r.first + r.second) * sizeof(UINT64) };
        UINT64* mapped = nullptr;
        buf->Map(0, &readRange, reinterpret_cast<void**>(&mapped));

        // For each even offset in this range, compute the passIndex and update EMA
        for (unsigned idx = r.first; idx < r.first + r.second; ++idx) {
            unsigned frameBase = frameIndex * m_numPasses * 2;
            unsigned rel = idx - frameBase;
            if (rel % 2 != 0) continue; // only start stamps

            unsigned passIndex = rel / 2;
            UINT64 t0 = mapped[frameBase + passIndex * 2 + 0];
            UINT64 t1 = mapped[frameBase + passIndex * 2 + 1];
            double durationMs = double(t1 - t0) * 1000.0 / double(m_gpuTimestampFrequency);

            auto& stat = m_stats[passIndex];
            stat.ema = stat.ema * (1.0 - PassStats::alpha)
                + durationMs * PassStats::alpha;
        }
        buf->Unmap(0, nullptr);
    }

    // Cleanup for this frame
    pending.clear();
    m_recordedQueries[queue].erase(frameIndex);
}

void StatisticsManager::ClearAll() {
    // Reset core state
    m_queryHeap.Reset();
    m_numPasses = 0;
    m_passNames.clear();
    m_stats.clear();

    // Clear per-queue resources and data
    for (auto& kv : m_readbackBuffers) {
        kv.second.Reset();
    }
    m_readbackBuffers.clear();
    m_recordedQueries.clear();
    m_pendingResolves.clear();
}