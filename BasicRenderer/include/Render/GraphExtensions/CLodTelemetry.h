#pragma once

#include <array>
#include <atomic>
#include <cstdint>

enum class CLodWorkGraphCounterIndex : uint32_t {
    ObjectCullThreads = 0,
    ObjectCullInRangeThreads,
    ObjectCullVisibleThreads,
    ObjectCullTraverseRecordsEmitted,

    TraverseNodesThreads,
    TraverseNodesInternalNodeRecords,
    TraverseNodesLeafNodeRecords,
    TraverseNodesCulledNodeRecords,
    TraverseNodesRejectedByErrorRecords,
    TraverseNodesActiveChildThreads,
    TraverseNodesTraverseRecordsEmitted,
    TraverseNodesGroupRecordsEmitted,

    GroupEvaluateThreads,
    GroupEvaluateGroupRecords,
    GroupEvaluateCulledGroupRecords,
    GroupEvaluateRejectedByErrorRecords,
    GroupEvaluateActiveChildThreads,
    GroupEvaluateEmitBucketThreads,
    GroupEvaluateRefinedTraversalThreads,
    GroupEvaluateNonResidentRefinedChildThreads,
    GroupEvaluateNonResidentFallbackBucketThreads,

    ClusterCullThreads,
    ClusterCullInRangeThreads,
    ClusterCullWaves,
    ClusterCullActiveLanes,
    ClusterCullSurvivingLanes,
    ClusterCullZeroSurvivorWaves,
    ClusterCullVisibleClusterWrites,

    TraverseNodesCoalescedLaunches,
    TraverseNodesCoalescedInputRecords,
    TraverseNodesCoalescedInputCount1,
    TraverseNodesCoalescedInputCount2,
    TraverseNodesCoalescedInputCount3,
    TraverseNodesCoalescedInputCount4,
    TraverseNodesCoalescedInputCount5,
    TraverseNodesCoalescedInputCount6,
    TraverseNodesCoalescedInputCount7,
    TraverseNodesCoalescedInputCount8,

    GroupEvaluateCoalescedLaunches,
    GroupEvaluateCoalescedInputRecords,
    GroupEvaluateCoalescedInputCount1,
    GroupEvaluateCoalescedInputCount2,
    GroupEvaluateCoalescedInputCount3,
    GroupEvaluateCoalescedInputCount4,
    GroupEvaluateCoalescedInputCount5,
    GroupEvaluateCoalescedInputCount6,
    GroupEvaluateCoalescedInputCount7,
    GroupEvaluateCoalescedInputCount8,

    Phase1OcclusionNodeReplayEnqueueAttempts,
    Phase1OcclusionGroupReplayEnqueueAttempts,
    Phase1OcclusionClusterReplayEnqueueAttempts,

    Phase2ReplayNodeGroupLaunches,
    Phase2ReplayNodeGroupInputRecords,
    Phase2ReplayNodeInputRecords,
    Phase2ReplayGroupInputRecords,
    Phase2ReplayNodeRecordsEmitted,
    Phase2ReplayGroupRecordsEmitted,

    Phase2ReplayMeshletLaunches,
    Phase2ReplayMeshletInputRecords,
    Phase2ReplayMeshletBucketRecordsEmitted,

    Phase2ReplayTraverseRecordsConsumed,
    Phase2ReplayGroupRecordsConsumed,
    Phase2ReplayClusterBucketRecordsConsumed,

    Count
};

inline constexpr uint32_t CLodWorkGraphCounterCount =
    static_cast<uint32_t>(CLodWorkGraphCounterIndex::Count);

struct CLodWorkGraphTelemetryCounters {
    std::array<uint32_t, CLodWorkGraphCounterCount> counters{};
};

inline std::atomic<uint32_t> g_clodWorkGraphTelemetryEnabled = 0u;

inline void SetCLodWorkGraphTelemetryEnabled(bool enabled) {
    g_clodWorkGraphTelemetryEnabled.store(enabled ? 1u : 0u, std::memory_order_relaxed);
}

inline bool IsCLodWorkGraphTelemetryEnabled() {
    return g_clodWorkGraphTelemetryEnabled.load(std::memory_order_relaxed) != 0u;
}

struct CLodStreamingOperationStats {
    uint32_t loadRequested = 0;
    uint32_t loadUnique = 0;
    uint32_t loadApplied = 0;
    uint32_t loadFailed = 0;

    uint32_t unloadRequested = 0;
    uint32_t unloadUnique = 0;
    uint32_t unloadApplied = 0;
    uint32_t unloadFailed = 0;
};

inline std::atomic<uint64_t> g_clodStreamingOperationStatsSequence = 0;
inline std::atomic<uint32_t> g_clodStreamingLoadRequested = 0;
inline std::atomic<uint32_t> g_clodStreamingLoadUnique = 0;
inline std::atomic<uint32_t> g_clodStreamingLoadApplied = 0;
inline std::atomic<uint32_t> g_clodStreamingLoadFailed = 0;
inline std::atomic<uint32_t> g_clodStreamingUnloadRequested = 0;
inline std::atomic<uint32_t> g_clodStreamingUnloadUnique = 0;
inline std::atomic<uint32_t> g_clodStreamingUnloadApplied = 0;
inline std::atomic<uint32_t> g_clodStreamingUnloadFailed = 0;

inline void PublishCLodStreamingOperationStats(const CLodStreamingOperationStats& stats) {
    g_clodStreamingLoadRequested.store(stats.loadRequested, std::memory_order_relaxed);
    g_clodStreamingLoadUnique.store(stats.loadUnique, std::memory_order_relaxed);
    g_clodStreamingLoadApplied.store(stats.loadApplied, std::memory_order_relaxed);
    g_clodStreamingLoadFailed.store(stats.loadFailed, std::memory_order_relaxed);

    g_clodStreamingUnloadRequested.store(stats.unloadRequested, std::memory_order_relaxed);
    g_clodStreamingUnloadUnique.store(stats.unloadUnique, std::memory_order_relaxed);
    g_clodStreamingUnloadApplied.store(stats.unloadApplied, std::memory_order_relaxed);
    g_clodStreamingUnloadFailed.store(stats.unloadFailed, std::memory_order_relaxed);

    g_clodStreamingOperationStatsSequence.fetch_add(1u, std::memory_order_relaxed);
}

inline bool TryReadCLodStreamingOperationStats(uint64_t& inOutSequence, CLodStreamingOperationStats& outStats) {
    const uint64_t sequence = g_clodStreamingOperationStatsSequence.load(std::memory_order_relaxed);
    if (sequence == inOutSequence) {
        return false;
    }

    outStats.loadRequested = g_clodStreamingLoadRequested.load(std::memory_order_relaxed);
    outStats.loadUnique = g_clodStreamingLoadUnique.load(std::memory_order_relaxed);
    outStats.loadApplied = g_clodStreamingLoadApplied.load(std::memory_order_relaxed);
    outStats.loadFailed = g_clodStreamingLoadFailed.load(std::memory_order_relaxed);

    outStats.unloadRequested = g_clodStreamingUnloadRequested.load(std::memory_order_relaxed);
    outStats.unloadUnique = g_clodStreamingUnloadUnique.load(std::memory_order_relaxed);
    outStats.unloadApplied = g_clodStreamingUnloadApplied.load(std::memory_order_relaxed);
    outStats.unloadFailed = g_clodStreamingUnloadFailed.load(std::memory_order_relaxed);

    inOutSequence = sequence;
    return true;
}
