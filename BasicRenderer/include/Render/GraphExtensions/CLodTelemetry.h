#pragma once

#include <array>
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

    Count
};

inline constexpr uint32_t CLodWorkGraphCounterCount =
    static_cast<uint32_t>(CLodWorkGraphCounterIndex::Count);

struct CLodWorkGraphTelemetryCounters {
    std::array<uint32_t, CLodWorkGraphCounterCount> counters{};
};
