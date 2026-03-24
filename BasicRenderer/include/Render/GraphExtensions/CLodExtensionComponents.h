#pragma once

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

struct VisibleClustersBufferTag {};
struct VisibleClustersCounterTag {};
struct CLodWorkGraphTelemetryBufferTag {};
struct CLodDeepVisibilityCounterTag {};
struct CLodDeepVisibilityOverflowCounterTag {};
struct CLodDeepVisibilityStatsTag {};
