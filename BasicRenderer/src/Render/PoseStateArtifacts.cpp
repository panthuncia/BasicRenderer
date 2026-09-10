#include "Render/PoseStateArtifacts.h"

#include "Render/PublishedRendererState.h"

namespace br::render {
namespace {

ArtifactBuildResult BuildPoseState(const ArtifactBuildContext& context) {
    const auto input = context.input.Get<PoseStateBuildInput>();
    if (!input || input->activeInstanceRevision != context.revision) {
        return ArtifactBuildResult::Failure("pose-state input/revision mismatch");
    }
    auto state = std::make_shared<PublishedPoseState>();
    state->activeInstanceRevision = input->activeInstanceRevision;
    state->activeInstances = input->activeInstances;
    state->retainedResources = input->retainedResources;

    auto root = std::make_shared<RendererStateFragmentArtifact>();
    root->kind = PublishedFragmentKind::Poses;
    root->fragment.revision = context.revision;
    root->fragment.payload = ArtifactPayload::Make<PublishedPoseState>(state);
    root->fragment.resourceHolds.reserve(state->retainedResources.size());
    for (const auto& resource : state->retainedResources) root->fragment.resourceHolds.push_back(resource);
    return ArtifactBuildResult::Ready(
        ArtifactPayload::Make<RendererStateFragmentArtifact>(std::move(root)));
}

} // namespace

void RegisterPoseStateProducer(AsyncStateGraph& graph) {
    graph.RegisterProducer(ArtifactKind::PoseState, {
        TaskLane::FrameCritical, TaskDomain::GraphPublication,
        "PoseStateArtifact::Build", BuildPoseState });
}

} // namespace br::render
