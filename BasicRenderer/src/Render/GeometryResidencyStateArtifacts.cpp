#include "Render/GeometryResidencyStateArtifacts.h"

#include <algorithm>

#include "Render/PublishedRendererState.h"

namespace br::render {
namespace {

ArtifactBuildResult BuildGeometryResidencyState(const ArtifactBuildContext& context) {
    const auto input = context.input.Get<GeometryResidencyDeltaInput>();
    if (!input || context.revision == 0) {
        return ArtifactBuildResult::Failure("geometry-residency input mismatch");
    }

    auto state = std::make_shared<PublishedGeometryResidencyState>();
    for (const auto& dependency : context.dependencies) {
        if (dependency.key.kind != ArtifactKind::GeometryResidency) continue;
        const auto predecessorRoot = dependency.payload.Get<RendererStateFragmentArtifact>();
        const auto predecessor = predecessorRoot
            ? predecessorRoot->fragment.payload.Get<PublishedGeometryResidencyState>() : nullptr;
        if (predecessor) {
            *state = *predecessor;
            break;
        }
    }

    if (input->kind == GeometryResidencyDeltaKind::Reset) {
        state->activeRanges = input->resetRanges;
    } else {
        auto existing = std::ranges::find_if(state->activeRanges,
            [&](const GeometryResidencyRange& range) {
                return range.groupsBase == input->range.groupsBase;
            });
        if (input->kind == GeometryResidencyDeltaKind::Remove) {
            if (existing != state->activeRanges.end()) state->activeRanges.erase(existing);
        } else if (input->range.groupCount != 0) {
            if (existing != state->activeRanges.end()) *existing = input->range;
            else state->activeRanges.push_back(input->range);
        }
    }

    std::ranges::sort(state->activeRanges, {}, &GeometryResidencyRange::groupsBase);
    state->revision = context.revision;
    state->maxTraversalDepth = 0;
    state->maxGroupIndex = 0;
    for (const auto& range : state->activeRanges) {
        state->maxTraversalDepth = std::max(state->maxTraversalDepth, range.maxTraversalDepth);
        state->maxGroupIndex = std::max(state->maxGroupIndex,
            range.groupsBase + range.groupCount);
    }

    auto root = std::make_shared<RendererStateFragmentArtifact>();
    root->kind = PublishedFragmentKind::GeometryResidency;
    root->fragment.revision = context.revision;
    // The predecessor is build history, not a publication dependency. The
    // successor owns a complete immutable value and can retire independently.
    root->fragment.payload = ArtifactPayload::Make<PublishedGeometryResidencyState>(state);
    return ArtifactBuildResult::Ready(
        ArtifactPayload::Make<RendererStateFragmentArtifact>(std::move(root)));
}

} // namespace

void RegisterGeometryResidencyStateProducer(AsyncStateGraph& graph) {
    graph.RegisterProducer(ArtifactKind::GeometryResidency, {
        TaskLane::Streaming, TaskDomain::GraphPublication,
        "GeometryResidencyStateArtifact::Build", BuildGeometryResidencyState });
}

} // namespace br::render
