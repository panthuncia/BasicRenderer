#include "Managers/Singletons/StatisticsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include <DirectX/d3dx12.h>
#include <algorithm>

StatisticsManager& StatisticsManager::GetInstance() {
    static StatisticsManager inst;
    return inst;
}

void StatisticsManager::Initialize() {
    m_numFramesInFlight = SettingsManager::GetInstance()
        .getSettingGetter<uint8_t>("numFramesInFlight")();
    auto queue = DeviceManager::GetInstance().GetGraphicsQueue();
    queue->GetTimestampFrequency(&m_gpuTimestampFreq);

    auto& device = DeviceManager::GetInstance().GetDevice();
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 opts9 = {};
    device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS9,
        &opts9, sizeof(opts9)
    );
    assert(opts9.MeshShaderPipelineStatsSupported);
}

void StatisticsManager::RegisterPasses(const std::vector<std::string>& passNames) {
    m_passNames = passNames;
    m_numPasses = static_cast<unsigned>(passNames.size());
    m_stats.assign( m_numPasses, {} );
    m_isGeometryPass.assign(m_numPasses, false);
    m_meshStatsEma.assign(m_numPasses, {});
}

unsigned StatisticsManager::RegisterPass(const std::string& passName) {
    m_passNames.push_back(passName);
    m_numPasses = static_cast<unsigned>(m_passNames.size());
    m_stats.emplace_back();
    m_isGeometryPass.push_back(false);
    m_meshStatsEma.emplace_back();
    return m_numPasses - 1;
}

void StatisticsManager::MarkGeometryPass(const std::string& passName) {
    auto it = std::find(m_passNames.begin(), m_passNames.end(), passName);
    if (it != m_passNames.end()) {
        m_isGeometryPass[it - m_passNames.begin()] = true;
    }
}

void StatisticsManager::RegisterQueue(ID3D12CommandQueue* queue) {
    m_timestampBuffers[queue];
    m_meshStatsBuffers[queue];
    m_recordedQueries[queue];
    m_pendingResolves[queue];
}

void StatisticsManager::SetupQueryHeap() {
    auto device = DeviceManager::GetInstance().GetDevice();

    // Timestamp heap: 2 queries/pass/frame
    D3D12_QUERY_HEAP_DESC th = { D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
        m_numPasses * 2 * m_numFramesInFlight };
    device->CreateQueryHeap(&th, IID_PPV_ARGS(&m_timestampHeap));

    // Mesh stats heap: 1 query/pass/frame
    D3D12_QUERY_HEAP_DESC ph = { D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1,
        m_numPasses * m_numFramesInFlight };
    device->CreateQueryHeap(&ph, IID_PPV_ARGS(&m_pipelineStatsHeap));

    // Allocate readback buffers for each queue
    UINT64 tsSize = sizeof(UINT64) * th.Count;
    auto tsDesc = CD3DX12_RESOURCE_DESC::Buffer(tsSize);
    UINT64 psSize = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1) * ph.Count;
    auto psDesc = CD3DX12_RESOURCE_DESC::Buffer(psSize);

    for (auto& kv : m_timestampBuffers) {
        auto props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        device->CreateCommittedResource(
            &props,
            D3D12_HEAP_FLAG_NONE, &tsDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&kv.second)
        );
        auto& mb = m_meshStatsBuffers[kv.first];
        device->CreateCommittedResource(
            &props,
            D3D12_HEAP_FLAG_NONE, &psDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&mb)
        );
    }
}

void StatisticsManager::BeginQuery(unsigned passIndex,
    unsigned frameIndex,
    ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* cmd) {
    if (passIndex >= m_numPasses) return;
    UINT tsIdx = (frameIndex*m_numPasses + passIndex)*2;
    cmd->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsIdx);

    if (m_isGeometryPass[passIndex]) {
        UINT psIdx = frameIndex*m_numPasses + passIndex;
        cmd->BeginQuery(m_pipelineStatsHeap.Get(),
            D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,
            psIdx);
    }
    m_recordedQueries[queue][frameIndex].push_back(tsIdx);
}

void StatisticsManager::EndQuery(unsigned passIndex,
    unsigned frameIndex,
    ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* cmd) {
    if (passIndex >= m_numPasses) return;
    UINT tsIdx = (frameIndex*m_numPasses + passIndex)*2 + 1;
    cmd->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, tsIdx);

    if (m_isGeometryPass[passIndex]) {
        UINT psIdx = frameIndex*m_numPasses + passIndex;
        cmd->EndQuery(m_pipelineStatsHeap.Get(),
            D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,
            psIdx);
    }
    m_recordedQueries[queue][frameIndex].push_back(tsIdx);
}

void StatisticsManager::ResolveQueries(unsigned frameIndex,
    ID3D12CommandQueue* queue,
    ID3D12GraphicsCommandList* cmd) {
    auto& rec = m_recordedQueries[queue][frameIndex];
    if (rec.empty()) return;

    std::sort(rec.begin(), rec.end());
    std::vector<std::pair<unsigned, unsigned>> ranges;
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

    auto& tsBuf = m_timestampBuffers[queue];
    auto& psBuf = m_meshStatsBuffers[queue];

    // Resolve timestamp data and record for OnFrameComplete
    for (auto& r : ranges) {
        cmd->ResolveQueryData(
            m_timestampHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            r.first, r.second,
            tsBuf.Get(), sizeof(UINT64) * r.first
        );
        m_pendingResolves[queue][frameIndex].push_back(r);

        // For each stamped pass in this range, resolve mesh-stats if geometry
        for (unsigned idx = r.first; idx < r.first + r.second; idx += 2) {
            unsigned base = idx / 2;
            unsigned pi   = base % m_numPasses;
            if (!m_isGeometryPass[pi]) continue;
            UINT psIdx = frameIndex * m_numPasses + pi;
            cmd->ResolveQueryData(
                m_pipelineStatsHeap.Get(),
                D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,
                psIdx, 1,
                psBuf.Get(), sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1) * psIdx
            );
        }
    }

    rec.clear();
}

void StatisticsManager::OnFrameComplete(unsigned frameIndex,
    ID3D12CommandQueue* queue) {
    auto& buf = m_timestampBuffers[queue];
    auto& psBuf = m_meshStatsBuffers[queue];
    auto& pending = m_pendingResolves[queue][frameIndex];
    if (pending.empty()) return;

    for (auto& r:pending) {
        D3D12_RANGE readRange{ r.first*sizeof(UINT64), (r.first+r.second)*sizeof(UINT64) };
        UINT64* mappedTs=nullptr;
        buf->Map(0,&readRange,(void**)&mappedTs);

        for (unsigned idx=r.first; idx<r.first+r.second; idx+=2) {
            unsigned base = idx/2;
            unsigned fi = base/m_numPasses;
            unsigned pi = base% m_numPasses;
            UINT64 t0 = mappedTs[idx];
            UINT64 t1 = mappedTs[idx+1];
            double ms = double(t1-t0)*1000.0/double(m_gpuTimestampFreq);
            m_stats[pi].ema = m_stats[pi].ema*(1.0-PassStats::alpha) + ms*PassStats::alpha;

            if (m_isGeometryPass[pi]) {
                // mesh-stats resolve
                D3D12_RANGE psRange{
                    (fi*m_numPasses + pi) * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1),
                    ((fi*m_numPasses + pi) + 1) * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1)
                };

                void* mappedMem = nullptr;
                psBuf->Map(0, &psRange, &mappedMem);

                BYTE* basePtr = reinterpret_cast<BYTE*>(mappedMem);
                auto* stats   = reinterpret_cast<D3D12_QUERY_DATA_PIPELINE_STATISTICS1*>(
                    basePtr + psRange.Begin
                    );
                double inv = double(stats->MSInvocations);
                double prim = double(stats->MSPrimitives);

                psBuf->Unmap(0, nullptr);

                auto& mps = m_meshStatsEma[pi];
                mps.invocationsEma = mps.invocationsEma * (1.0 - PassStats::alpha)
                    + inv * PassStats::alpha;
                mps.primitivesEma  = mps.primitivesEma  * (1.0 - PassStats::alpha)
                    + prim * PassStats::alpha;
            }
        }
    }
    buf->Unmap(0, nullptr);
    pending.clear();
    m_pendingResolves[queue].erase(frameIndex);
}

void StatisticsManager::ClearAll() {
    m_timestampHeap.Reset();
    m_pipelineStatsHeap.Reset();
    for (auto& kv:m_timestampBuffers) kv.second.Reset();
    for (auto& kv:m_meshStatsBuffers) kv.second.Reset();
    m_timestampBuffers.clear();
    m_meshStatsBuffers.clear();
    m_passNames.clear();
    m_stats.clear();
    m_isGeometryPass.clear();
    m_meshStatsEma.clear();
    m_recordedQueries.clear();
    m_pendingResolves.clear();
    m_numPasses=0;
}
