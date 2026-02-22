#include "Managers/Singletons/StatisticsManager.h"

#include <algorithm>
#include <cstring>
#include <rhi_helpers.h>
#include <spdlog/spdlog.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Render/Runtime/OpenRenderGraphSettings.h"

StatisticsManager& StatisticsManager::GetInstance() {
    static StatisticsManager inst;
    return inst;
}

void StatisticsManager::Initialize() {
    m_numFramesInFlight = rg::runtime::GetOpenRenderGraphSettings().numFramesInFlight;
	auto device = DeviceManager::GetInstance().GetDevice();
	m_gpuTimestampFreq = device.GetTimestampCalibration(rhi::QueueKind::Graphics).ticksPerSecond;
    m_getCollectPipelineStatistics = []() {
        return rg::runtime::GetOpenRenderGraphSettings().collectPipelineStatistics;
    };
}

void StatisticsManager::RegisterPasses(const std::vector<std::string>& passNames) {
    m_passNames = passNames;
    m_numPasses = static_cast<unsigned>(passNames.size());
    m_stats.assign(m_numPasses, {});
    m_isGeometryPass.assign(m_numPasses, false);
    m_meshStatsEma.assign(m_numPasses, {});
    m_passLastDataFrame.assign(m_numPasses, kNeverSeenFrame);
    m_passNameToIndex.clear();
    for (unsigned i = 0; i < m_numPasses; ++i) {
        if (!m_passNames[i].empty()) {
            m_passNameToIndex.emplace(m_passNames[i], i);
        }
    }
}

unsigned StatisticsManager::RegisterPass(const std::string& passName, bool isGeometryPass) {
    if (!passName.empty()) {
        auto it = m_passNameToIndex.find(passName);
        if (it != m_passNameToIndex.end()) {
            if (isGeometryPass) {
                m_isGeometryPass[it->second] = true;
            }
            return it->second;
        }
    }

    std::string resolvedName = passName;
    if (resolvedName.empty()) {
        resolvedName = "UnnamedPass#" + std::to_string(m_unnamedPassCounter++);
    }

    const unsigned index = static_cast<unsigned>(m_passNames.size());
    m_passNames.push_back(resolvedName);
    m_passNameToIndex[resolvedName] = index;

    m_numPasses = static_cast<unsigned>(m_passNames.size());
    m_stats.emplace_back();
    m_isGeometryPass.push_back(isGeometryPass);
    m_meshStatsEma.emplace_back();
    m_passLastDataFrame.push_back(kNeverSeenFrame);
    return index;
}

void StatisticsManager::BeginFrame() {
    ++m_frameSerial;

    rg::runtime::MemoryBudgetStats memoryBudgetStats{};
    memoryBudgetStats.sampleFrameSerial = m_frameSerial;
    if (auto* allocator = DeviceManager::GetInstance().GetAllocator()) {
        rhi::ma::Budget localBudget{};
        allocator->GetBudget(&localBudget, nullptr);
        memoryBudgetStats.usageBytes = localBudget.usageBytes;
        memoryBudgetStats.budgetBytes = localBudget.budgetBytes;
        memoryBudgetStats.valid = true;
    }
    m_memoryBudgetStats = memoryBudgetStats;
}

void StatisticsManager::RebuildVisiblePassIndices(uint64_t maxStaleFrames, std::vector<unsigned>& out) const {
    out.clear();
    out.reserve(m_passNames.size());

    const bool includeNeverSeen = (maxStaleFrames == (std::numeric_limits<uint64_t>::max)());

    for (unsigned i = 0; i < m_passNames.size(); ++i) {
        const uint64_t lastData = (i < m_passLastDataFrame.size()) ? m_passLastDataFrame[i] : kNeverSeenFrame;
        if (lastData == kNeverSeenFrame) {
            if (includeNeverSeen) {
                out.push_back(i);
            }
            continue;
        }

        const uint64_t missingFrames = (m_frameSerial >= lastData) ? (m_frameSerial - lastData) : 0;
        if (missingFrames <= maxStaleFrames) {
            out.push_back(i);
        }
    }
}

const std::vector<unsigned>& StatisticsManager::GetVisiblePassIndices() const {
    RebuildVisiblePassIndices(m_defaultMaxStaleFrames, m_visiblePassIndices);
    return m_visiblePassIndices;
}

const std::vector<unsigned>& StatisticsManager::GetVisiblePassIndices(uint64_t maxStaleFrames) const {
    RebuildVisiblePassIndices(maxStaleFrames, m_visiblePassIndices);
    return m_visiblePassIndices;
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
    if (m_timestampPool && m_pipelineStatsPool) {
        EnsureQueueBuffers(queueKind);
    }
}

void StatisticsManager::EnsureQueueBuffers(rhi::QueueKind queueKind) {
    if (!m_timestampPool || !m_pipelineStatsPool) {
        return;
    }

    auto device = DeviceManager::GetInstance().GetDevice();

    rhi::ResourceDesc tsRb = rhi::helpers::ResourceDesc::Buffer(
        static_cast<uint64_t>(m_timestampQueryInfo.elementSize) * m_timestampQueryInfo.count,
        rhi::HeapType::Readback);
    rhi::ResourceDesc psRb = rhi::helpers::ResourceDesc::Buffer(
        static_cast<uint64_t>(m_pipelineStatsQueryInfo.elementSize) * m_pipelineStatsQueryInfo.count,
        rhi::HeapType::Readback);

    auto& tsBuf = m_timestampBuffers[queueKind];
    auto& psBuf = m_meshStatsBuffers[queueKind];
    auto result = device.CreateCommittedResource(tsRb, tsBuf);
    result = device.CreateCommittedResource(psRb, psBuf);
}

void StatisticsManager::SetupQueryHeap() {
    auto device = DeviceManager::GetInstance().GetDevice();
    if (m_getCollectPipelineStatistics) {
        m_collectPipelineStatistics = m_getCollectPipelineStatistics();
    }

    if (m_numPasses == 0) {
        return;
    }

    if (m_queryPoolPassCapacity >= m_numPasses && m_timestampPool && m_pipelineStatsPool) {
        return;
    }

    m_queryPoolPassCapacity = m_numPasses;

    // Timestamp heap: 2 queries/pass/frame

	rhi::QueryPoolDesc tq;
    tq.type = rhi::QueryType::Timestamp;
    tq.count = m_queryPoolPassCapacity * 2 * m_numFramesInFlight;
    auto result = device.CreateQueryPool(tq, m_timestampPool);

	rhi::QueryPoolDesc sq;
	sq.type = rhi::QueryType::PipelineStatistics;
	sq.count = m_queryPoolPassCapacity * m_numFramesInFlight;
	sq.statsMask = rhi::PipelineStatBits::PS_MeshInvocations | rhi::PipelineStatBits::PS_MeshPrimitives;
	result = device.CreateQueryPool(sq, m_pipelineStatsPool);


    // Allocate readback buffers for each queue
    auto tsInfo = m_timestampPool->GetQueryResultInfo();
    auto psInfo = m_pipelineStatsPool->GetQueryResultInfo();

    rhi::ResourceDesc tsRb = rhi::helpers::ResourceDesc::Buffer(static_cast<uint64_t>(tsInfo.elementSize) * tsInfo.count, rhi::HeapType::Readback);
    rhi::ResourceDesc psRb = rhi::helpers::ResourceDesc::Buffer(static_cast<uint64_t>(psInfo.elementSize) * psInfo.count, rhi::HeapType::Readback);

    for (auto& kv : m_timestampBuffers) {
        auto& buf = kv.second;
        result = device.CreateCommittedResource(tsRb, buf);
        auto& mb = m_meshStatsBuffers[kv.first];
        result = std::move(device.CreateCommittedResource(psRb, mb));
	}

    m_timestampQueryInfo = m_timestampPool->GetQueryResultInfo();
    m_pipelineStatsQueryInfo = m_pipelineStatsPool->GetQueryResultInfo();
	m_pipelineStatsFields.resize(2);
    m_pipelineStatsFields[0].field = rhi::PipelineStatTypes::MeshInvocations;
    m_pipelineStatsFields[1].field = rhi::PipelineStatTypes::MeshPrimitives;
    m_pipelineStatsLayout = m_pipelineStatsPool->GetPipelineStatsLayout(m_pipelineStatsFields.data(), static_cast<uint32_t>(m_pipelineStatsFields.size()));

    m_recordedQueries.clear();
    m_pendingResolves.clear();
    for (auto& kv : m_timestampBuffers) {
        const auto qk = kv.first;
        m_recordedQueries[qk];
        m_pendingResolves[qk];
    }
}

void StatisticsManager::BeginQuery(
    unsigned passIndex,
    unsigned frameIndex,
    rhi::Queue& queue,
    rhi::CommandList& cmd)
{
    if (!m_timestampPool || passIndex >= m_numPasses || passIndex >= m_queryPoolPassCapacity) return;

    auto queueKind = queue.GetKind();
    auto tsIt = m_timestampBuffers.find(queueKind);
    if (tsIt == m_timestampBuffers.end() || !tsIt->second) return;

    const uint32_t frameBase = frameIndex * m_queryPoolPassCapacity;

    // Timestamp "begin" marker = write a timestamp at index 2*N
    const uint32_t tsIdx = (frameBase + passIndex) * 2u;
    cmd.WriteTimestamp(m_timestampPool->GetHandle(), tsIdx, rhi::Stage::Top); // RHI: EndQuery on a Timestamp pool writes a timestamp

    // Begin pipeline stats for geometry passes
    if (m_collectPipelineStatistics && m_isGeometryPass[passIndex]) {
        const uint32_t psIdx = frameBase + passIndex;
        cmd.BeginQuery(m_pipelineStatsPool->GetHandle(), psIdx);
    }
    m_recordedQueries[queueKind][frameIndex].push_back(tsIdx);
}

void StatisticsManager::EndQuery(
    unsigned passIndex,
    unsigned frameIndex,
    rhi::Queue& queue,
    rhi::CommandList& cmd)
{
    if (!m_timestampPool || passIndex >= m_numPasses || passIndex >= m_queryPoolPassCapacity) return;

    auto queueKind = queue.GetKind();
    auto tsIt = m_timestampBuffers.find(queueKind);
    if (tsIt == m_timestampBuffers.end() || !tsIt->second) return;

    const uint32_t frameBase = frameIndex * m_queryPoolPassCapacity;

    // Timestamp "end" marker = write a timestamp at index 2*N + 1
    const uint32_t tsIdx = (frameBase + passIndex) * 2u + 1u;
	cmd.WriteTimestamp(m_timestampPool->GetHandle(), tsIdx, rhi::Stage::Bottom); // RHI: EndQuery on a Timestamp pool writes a timestamp

    // End pipeline stats for geometry passes
    if (m_collectPipelineStatistics && m_isGeometryPass[passIndex]) {
        const uint32_t psIdx = frameBase + passIndex;
        cmd.EndQuery(m_pipelineStatsPool->GetHandle(), psIdx);
    }
    m_recordedQueries[queueKind][frameIndex].push_back(tsIdx);
}

void StatisticsManager::ResolveQueries(
    unsigned frameIndex,
    rhi::Queue& queue,
    rhi::CommandList& cmd)
{
    if (!m_timestampPool || m_timestampQueryInfo.elementSize == 0) return;

    auto queueKind = queue.GetKind();
    auto tsIt = m_timestampBuffers.find(queueKind);
    if (tsIt == m_timestampBuffers.end() || !tsIt->second) return;
    auto psIt = m_meshStatsBuffers.find(queueKind);
    if (psIt == m_meshStatsBuffers.end() || !psIt->second) return;

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

    const uint32_t frameBase = frameIndex * m_queryPoolPassCapacity;
    const uint64_t tsStride = m_timestampQueryInfo.elementSize; // usually 8
    const uint64_t psStride = m_pipelineStatsQueryInfo.elementSize; // backend-dependent

    auto& tsBuf = tsIt->second;   // rhi::ResourcePtr
    auto& psBuf = psIt->second;   // rhi::ResourcePtr

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
            const uint32_t encoded = idx / 2; // frameBase + passIndex
            if (encoded < frameBase) {
                continue;
            }
            const uint32_t pi = encoded - frameBase;
            if (pi >= m_numPasses) {
                continue;
            }
            if (!m_isGeometryPass[pi]) continue;

            const uint32_t psIdx = frameBase + pi;

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
	if (!m_timestampPool || m_timestampQueryInfo.elementSize == 0) return;

    if (m_getCollectPipelineStatistics) {
        m_collectPipelineStatistics = m_getCollectPipelineStatistics();
    }
	auto queueKind = queue.GetKind();
	auto tsIt = m_timestampBuffers.find(queueKind);
	auto psIt = m_meshStatsBuffers.find(queueKind);
	if (tsIt == m_timestampBuffers.end() || !tsIt->second) return;
	if (psIt == m_meshStatsBuffers.end() || !psIt->second) return;

    auto& tsBuf = tsIt->second;
    auto& psBuf = psIt->second;
    auto& pending = m_pendingResolves[queueKind][frameIndex];
    if (pending.empty()) return;

    const uint64_t tsStride = m_timestampQueryInfo.elementSize; // usually 8
    const uint64_t psStride = m_pipelineStatsQueryInfo.elementSize; // backend-specific
    const uint32_t frameBase = frameIndex * m_queryPoolPassCapacity;
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

            const uint32_t encoded = idx / 2; // frameBase + passIndex
            if (encoded < frameBase) {
                continue;
            }
            const uint32_t pi = encoded - frameBase;
            if (pi >= m_numPasses) {
                continue;
            }

            m_stats[pi].ema = m_stats[pi].ema * (1.0 - PassStats::alpha) + ms * PassStats::alpha;
            if (pi < m_passLastDataFrame.size()) {
                m_passLastDataFrame[pi] = m_frameSerial;
            }

            if (!m_collectPipelineStatistics || !m_isGeometryPass[pi]) continue;

            // Map just this pass's pipeline stat element
            const uint32_t psIdx = frameBase + pi;
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
    m_passNameToIndex.clear();
    m_passLastDataFrame.clear();
    m_visiblePassIndices.clear();
    m_recordedQueries.clear();
    m_pendingResolves.clear();
    m_numPasses=0;
    m_queryPoolPassCapacity = 0;
    m_unnamedPassCounter = 0;
    m_frameSerial = 0;
    m_memoryBudgetStats = {};
}
