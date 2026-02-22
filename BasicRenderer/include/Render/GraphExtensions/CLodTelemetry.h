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
