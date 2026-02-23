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
