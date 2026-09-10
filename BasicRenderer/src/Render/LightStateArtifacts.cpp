#include "Render/LightStateArtifacts.h"

#include "Render/PublishedRendererState.h"
#include "Render/ViewStateArtifacts.h"

namespace br::render {
namespace {

ArtifactBuildResult BuildLightTable(const ArtifactBuildContext& context) {
    const auto input = context.input.Get<LightTableBuildInput>();
    if (!input || input->revision != context.revision) {
        return ArtifactBuildResult::Failure("light-table input/revision mismatch");
    }
    auto state = std::make_shared<PublishedLightTableState>();
    state->revision = input->revision;
    state->lightCount = input->lightCount;
    state->lightPagePoolSize = input->lightPagePoolSize;
    if (!context.dependencies.empty()) {
        const auto viewsRoot = context.Dependency<RendererStateFragmentArtifact>(
            { ArtifactKind::ViewFamily, 0, 0 });
        if (!viewsRoot || viewsRoot.payload->kind != PublishedFragmentKind::Views ||
            !viewsRoot.payload->fragment.payload.Get<PublishedViewFamilyState>()) {
            return ArtifactBuildResult::Failure(
                "light-table exact view-family dependency is invalid");
        }
        state->viewFamilyRevision = viewsRoot.payload->fragment.revision;
    }
    state->directionalShadows = input->directionalShadows;
    state->retainedResources = input->retainedResources;

    auto root = std::make_shared<RendererStateFragmentArtifact>();
    root->kind = PublishedFragmentKind::Lights;
    root->fragment.revision = context.revision;
    root->fragment.payload = ArtifactPayload::Make<PublishedLightTableState>(state);
    root->fragment.resourceHolds.reserve(state->retainedResources.size());
    for (const auto& resource : state->retainedResources) root->fragment.resourceHolds.push_back(resource);
    return ArtifactBuildResult::Ready(
        ArtifactPayload::Make<RendererStateFragmentArtifact>(std::move(root)));
}

} // namespace

void RegisterLightStateProducer(AsyncStateGraph& graph) {
    graph.RegisterProducer(ArtifactKind::LightTable, {
        TaskLane::FrameCritical, TaskDomain::GraphPublication,
        "LightTableArtifact::Build", BuildLightTable });
}

} // namespace br::render
