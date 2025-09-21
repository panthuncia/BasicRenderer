#pragma once

#include <vector>

#include "Render/PipelineState.h"
#include "Utilities/HashMix.h"
#include "Render/RenderPhase.h"

struct MaterialKey {
    RenderPhase phase;
    PSOKey      pso;
    // Optional: ViewId / ShadowSplit / StereoEye to segregate per-view workloads
    bool operator==(const MaterialKey&) const = default;
};
struct MaterialKeyHash { size_t operator()(MaterialKey const& k) const noexcept { return util::hash_mix(k); } };