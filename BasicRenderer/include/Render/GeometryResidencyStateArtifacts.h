#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Render/AsyncStateGraph.h"

class PagePool;
namespace org { class ResourceGroup; }

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
    std::shared_ptr<PagePool> pagePool;
    std::shared_ptr<org::ResourceGroup> slabResources;
    std::uint64_t storageGeneration = 0;
};

struct PublishedGeometryResidencyState {
    std::uint64_t revision = 0;
    std::uint32_t maxTraversalDepth = 0;
    std::uint32_t maxGroupIndex = 0;
    std::vector<GeometryResidencyRange> activeRanges;
    // These retained owners make the publication self-contained. A frame that
    // selected this artifact remains valid even after a successor residency
    // generation is published or the manager facade is torn down.
    std::shared_ptr<PagePool> pagePool;
    std::shared_ptr<org::ResourceGroup> slabResources;
    std::uint64_t storageGeneration = 0;
};

void RegisterGeometryResidencyStateProducer(AsyncStateGraph& graph);

} // namespace br::render
