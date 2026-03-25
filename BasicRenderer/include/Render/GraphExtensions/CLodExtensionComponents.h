#pragma once

#include <cstdint>

enum class CLodExtensionType {
    VisiblityBuffer,
    AlphaBlend,
    Shadow,
};

struct CLodExtensionVisibilityBufferTag {};
struct CLodExtensionAlphaBlendTag {};
struct CLodExtensionShadowTag {};

struct CLodExtensionTypeTag {
};

struct CLodVisibleClusterCapacity {
    uint32_t maxVisibleClusters = 0u;
};

struct VisibleClustersBufferTag {};
struct VisibleClustersCounterTag {};
struct CLodWorkGraphTelemetryBufferTag {};
struct CLodReyesDiceQueueTag {};
struct CLodReyesFullClustersCounterTag {};
struct CLodReyesSplitQueueCounterATag {};
struct CLodReyesSplitQueueCounterBTag {};
struct CLodReyesSplitQueueOverflowATag {};
struct CLodReyesSplitQueueOverflowBTag {};
struct CLodReyesDiceQueueCounterTag {};
struct CLodReyesDiceQueueOverflowTag {};
struct CLodReyesTelemetryBufferPhase1Tag {};
struct CLodReyesTelemetryBufferPhase2Tag {};
struct CLodDeepVisibilityCounterTag {};
struct CLodDeepVisibilityOverflowCounterTag {};
struct CLodDeepVisibilityStatsTag {};
