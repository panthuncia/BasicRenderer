#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cstdint>

#include "Render/RenderGraph.h"

// Return true if the pass uses 'resourceId'.
using RGPassUsesResourceFn = std::function<bool(const void* passAndResources, uint64_t resourceId, bool isCompute)>;

struct RGInspectorOptions {
    // Horizontal placement within a batch (x spans [batch, batch+1])
    float blockLeftTransitions = 0.05f; // transitions start offset
    float blockWidthTransitions = 0.20f;
    float blockGap = 0.05f;
    float blockWidthPasses = 0.60f; // passes span
    float rowHeight = 1.0f;  // ImPlot units
    float laneSpacing = 1.2f;  // extra space between lanes
};

namespace RGInspector {
    void Show(const std::vector<RenderGraph::PassBatch>& batches,
        RGPassUsesResourceFn passUses = nullptr,
        const RGInspectorOptions& opts = {});
}
