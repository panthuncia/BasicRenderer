#pragma once

#include <cstdint>
#include <vector>

#include "Render/AsyncStateGraph.h"

namespace br::render {

struct GeometryResidencyRange {
    std::uint32_t groupsBase = 0;
    std::uint32_t groupCount = 0;
    std::uint32_t maxTraversalDepth = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> coarsestRanges;
    auto operator<=>(const GeometryResidencyRange&) const = default;
};

enum class GeometryResidencyDeltaKind : std::uint8_t {
    AddOrReplace,
    Remove,
    Reset,
};

struct GeometryResidencyDeltaInput {
    GeometryResidencyDeltaKind kind = GeometryResidencyDeltaKind::Reset;
    GeometryResidencyRange range;
    std::vector<GeometryResidencyRange> resetRanges;
};

struct PublishedGeometryResidencyState {
    std::uint64_t revision = 0;
    std::uint32_t maxTraversalDepth = 0;
    std::uint32_t maxGroupIndex = 0;
    std::vector<GeometryResidencyRange> activeRanges;
};

void RegisterGeometryResidencyStateProducer(AsyncStateGraph& graph);

} // namespace br::render
