#include "Managers/Singletons/StatisticsManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include <algorithm>
#include <rhi_helpers.h>

StatisticsManager& StatisticsManager::GetInstance() {
    static StatisticsManager inst;
    return inst;
}

void StatisticsManager::Initialize() {
    m_numFramesInFlight = SettingsManager::GetInstance()
        .getSettingGetter<uint8_t>("numFramesInFlight")();
	auto device = DeviceManager::GetInstance().GetDevice();
	m_gpuTimestampFreq = device.GetTimestampCalibration(rhi::QueueKind::Graphics).ticksPerSecond;
	m_getCollectPipelineStatistics =
		SettingsManager::GetInstance().getSettingGetter<bool>("collectPipelineStatistics");
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

void StatisticsManager::RegisterQueue(rhi::QueueKind queueKind) {
    m_timestampBuffers[queueKind];
    m_meshStatsBuffers[queueKind];
    m_recordedQueries[queueKind];
    m_pendingResolves[queueKind];
}

void StatisticsManager::SetupQueryHeap() {
    auto device = DeviceManager::GetInstance().GetDevice();
    if (m_numPasses == 0) {
		spdlog::warn("No passes registered for StatisticsManager, skipping query heap setup.");
		return;
    }
    // Timestamp heap: 2 queries/pass/frame

	rhi::QueryPoolDesc tq;
    tq.type = rhi::QueryType::Timestamp;
    tq.count = m_numPasses * 2 * m_numFramesInFlight;
    m_timestampPool = device.CreateQueryPool(tq);

	rhi::QueryPoolDesc sq;
	sq.type = rhi::QueryType::PipelineStatistics;
	sq.count = m_numPasses * m_numFramesInFlight;
	sq.statsMask = rhi::PipelineStatBits::PS_MeshInvocations | rhi::PipelineStatBits::PS_MeshPrimitives;
	m_pipelineStatsPool = device.CreateQueryPool(sq);


    // Allocate readback buffers for each queue
    auto tsInfo = m_timestampPool->GetQueryResultInfo();
    auto psInfo = m_pipelineStatsPool->GetQueryResultInfo();

    rhi::ResourceDesc tsRb = rhi::helpers::ResourceDesc::Buffer(uint64_t(tsInfo.elementSize) * tsInfo.count, rhi::HeapType::Readback);
    rhi::ResourceDesc psRb = rhi::helpers::ResourceDesc::Buffer(uint64_t(psInfo.elementSize) * psInfo.count, rhi::HeapType::Readback);

    for (auto& kv : m_timestampBuffers) {
        auto& buf = kv.second;
        buf = device.CreateCommittedResource(tsRb);
        auto& mb = m_meshStatsBuffers[kv.first];
        mb = std::move(device.CreateCommittedResource(psRb));
	}

    m_timestampQueryInfo = m_timestampPool->GetQueryResultInfo();
    m_pipelineStatsQueryInfo = m_pipelineStatsPool->GetQueryResultInfo();
	m_pipelineStatsFields.resize(2);
    m_pipelineStatsFields[0].field = rhi::PipelineStatTypes::MeshInvocations;
    m_pipelineStatsFields[1].field = rhi::PipelineStatTypes::MeshPrimitives;
    m_pipelineStatsLayout = m_pipelineStatsPool->GetPipelineStatsLayout(m_pipelineStatsFields.data(), static_cast<uint32_t>(m_pipelineStatsFields.size()));
}

void StatisticsManager::BeginQuery(
    unsigned passIndex,
    unsigned frameIndex,
    rhi::Queue& queue,
    rhi::CommandList& cmd)
{
    if (passIndex >= m_numPasses) return;

    // Timestamp "begin" marker = write a timestamp at index 2*N
    const uint32_t tsIdx = (frameIndex * m_numPasses + passIndex) * 2u;
    cmd.WriteTimestamp(m_timestampPool->GetHandle(), tsIdx, rhi::Stage::Top); // RHI: EndQuery on a Timestamp pool writes a timestamp

    // Begin pipeline stats for geometry passes
    if (m_collectPipelineStatistics && m_isGeometryPass[passIndex]) {
        const uint32_t psIdx = frameIndex * m_numPasses + passIndex;
        cmd.BeginQuery(m_pipelineStatsPool->GetHandle(), psIdx);
    }
	auto queueKind = queue.GetKind();
    m_recordedQueries[queueKind][frameIndex].push_back(tsIdx);
}

void StatisticsManager::EndQuery(
    unsigned passIndex,
    unsigned frameIndex,
    rhi::Queue& queue,
    rhi::CommandList& cmd)
{
    if (passIndex >= m_numPasses) return;

    // Timestamp "end" marker = write a timestamp at index 2*N + 1
    const uint32_t tsIdx = (frameIndex * m_numPasses + passIndex) * 2u + 1u;
	cmd.WriteTimestamp(m_timestampPool->GetHandle(), tsIdx, rhi::Stage::Bottom); // RHI: EndQuery on a Timestamp pool writes a timestamp

    // End pipeline stats for geometry passes
    if (m_collectPipelineStatistics && m_isGeometryPass[passIndex]) {
        const uint32_t psIdx = frameIndex * m_numPasses + passIndex;
        cmd.EndQuery(m_pipelineStatsPool->GetHandle(), psIdx);
    }
	auto queueKind = queue.GetKind();
    m_recordedQueries[queueKind][frameIndex].push_back(tsIdx);
}

void StatisticsManager::ResolveQueries(
    unsigned frameIndex,
    rhi::Queue& queue,
    rhi::CommandList& cmd)
{
    auto queueKind = queue.GetKind();
    auto& rec = m_recordedQueries[queueKind][frameIndex];
    if (rec.empty()) return;

    // Collapse timestamp indices into contiguous ranges
    std::sort(rec.begin(), rec.end());
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    uint32_t start = rec[0], prev = rec[0];
    for (size_t i = 1; i < rec.size(); ++i) {
        if (rec[i] == prev + 1) {
            prev = rec[i];
        }
        else {
            ranges.emplace_back(start, prev - start + 1);
            start = rec[i];
            prev = rec[i];
        }
    }
    ranges.emplace_back(start, prev - start + 1);

    const uint64_t tsStride = m_timestampQueryInfo.elementSize; // usually 8
    const uint64_t psStride = m_pipelineStatsQueryInfo.elementSize; // backend-dependent

    auto& tsBuf = m_timestampBuffers[queueKind];   // rhi::ResourcePtr
    auto& psBuf = m_meshStatsBuffers[queueKind];   // rhi::ResourcePtr

    // Resolve timestamp data and remember what to read on frame complete
    for (auto& r : ranges) {
        // Write timestamp results starting at byte offset = stride * firstIndex
        cmd.ResolveQueryData(
            m_timestampPool->GetHandle(),
            r.first, r.second,
            tsBuf->GetHandle(),
            tsStride * uint64_t(r.first)
        );

        m_pendingResolves[queueKind][frameIndex].push_back(r);

        if (!m_collectPipelineStatistics) continue;

        // For each stamped pass in this range, resolve pipeline stats if it's a geometry pass
        for (uint32_t idx = r.first; idx < r.first + r.second; idx += 2) {
            const uint32_t base = idx / 2;                 // pass instance index
            const uint32_t pi = base % m_numPasses;      // pass id within frame
            if (!m_isGeometryPass[pi]) continue;

            const uint32_t psIdx = frameIndex * m_numPasses + pi;

            cmd.ResolveQueryData(
                m_pipelineStatsPool->GetHandle(),
                psIdx, 1,
                psBuf->GetHandle(),
                psStride * uint64_t(psIdx)
            );
        }
    }

    rec.clear();
}

void StatisticsManager::OnFrameComplete(
    unsigned frameIndex,
    rhi::Queue& queue)
{
    m_collectPipelineStatistics = m_getCollectPipelineStatistics();
	auto queueKind = queue.GetKind();
    auto& tsBuf = m_timestampBuffers[queueKind];
    auto& psBuf = m_meshStatsBuffers[queueKind];
    auto& pending = m_pendingResolves[queueKind][frameIndex];
    if (pending.empty()) return;

    const uint64_t tsStride = m_timestampQueryInfo.elementSize; // usually 8
    const uint64_t psStride = m_pipelineStatsQueryInfo.elementSize; // backend-specific
    const double   toMs = 1000.0 / double(m_gpuTimestampFreq);

    auto readU64At = [](const uint8_t* base, uint64_t byteOffset) -> uint64_t {
        uint64_t v = 0;
        std::memcpy(&v, base + byteOffset, sizeof(uint64_t));
        return v;
        };

	// TODO: Avoid searching the field list every time
    auto findFieldOffset = [&](rhi::PipelineStatTypes f, uint32_t& off) -> bool {
        for (const auto& fd : m_pipelineStatsFields) {
            if (fd.field == f && fd.supported) { off = fd.byteOffset; return true; }
        }
        return false;
        };

    for (const auto& r : pending) {
        // Map just the timestamp byte range we resolved this frame
        const uint64_t tsMapOffset = tsStride * uint64_t(r.first);
        const uint64_t tsMapSize = tsStride * uint64_t(r.second);

        void* tsPtrVoid = nullptr;
        tsBuf->Map(&tsPtrVoid, tsMapOffset, tsMapSize);
        const uint8_t* tsBase = static_cast<const uint8_t*>(tsPtrVoid);

        for (uint32_t idx = r.first; idx < r.first + r.second; idx += 2) {
            const uint32_t local0 = (idx - r.first);
            const uint32_t local1 = local0 + 1;

            // Read the two timestamps (each element starts at localIndex * tsStride)
            const uint64_t t0 = readU64At(tsBase, uint64_t(local0) * tsStride);
            const uint64_t t1 = readU64At(tsBase, uint64_t(local1) * tsStride);
            const double   ms = double(t1 - t0) * toMs;

            const uint32_t base = idx / 2;
            const uint32_t fi = base / m_numPasses;
            const uint32_t pi = base % m_numPasses;

            m_stats[pi].ema = m_stats[pi].ema * (1.0 - PassStats::alpha) + ms * PassStats::alpha;

            if (!m_collectPipelineStatistics || !m_isGeometryPass[pi]) continue;

            // Map just this pass's pipeline stat element
            const uint32_t psIdx = frameIndex * m_numPasses + pi;
            const uint64_t psOffset = psStride * uint64_t(psIdx);
            void* psPtrVoid = nullptr;
            psBuf->Map(&psPtrVoid, psOffset, psStride);

            const uint8_t* psBase = static_cast<const uint8_t*>(psPtrVoid);

            uint64_t inv = 0, prim = 0;
            uint32_t offInv = 0, offPrim = 0;

            if (findFieldOffset(rhi::PipelineStatTypes::MeshInvocations, offInv))
                inv = readU64At(psBase, offInv);
            if (findFieldOffset(rhi::PipelineStatTypes::MeshPrimitives, offPrim))
                prim = readU64At(psBase, offPrim);

            psBuf->Unmap(0, 0);

            auto& mps = m_meshStatsEma[pi];
            mps.invocationsEma = mps.invocationsEma * (1.0 - PassStats::alpha) + double(inv) * PassStats::alpha;
            mps.primitivesEma = mps.primitivesEma * (1.0 - PassStats::alpha) + double(prim) * PassStats::alpha;
        }

        tsBuf->Unmap(0, 0);
    }

    pending.clear();
    m_pendingResolves[queueKind].erase(frameIndex);
}


void StatisticsManager::ClearAll() {
    m_timestampPool.Reset();
    m_pipelineStatsPool.Reset();
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
