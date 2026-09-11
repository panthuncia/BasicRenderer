#include "Render/ViewStateArtifacts.h"

#include "Render/PublishedRendererState.h"
#include "Render/VersionedGpuBufferArtifacts.h"

namespace br::render {
namespace {

ArtifactBuildResult BuildViewFamily(const ArtifactBuildContext& context) {
    const auto input = context.input.Get<ViewFamilyBuildInput>();
    if (!input || input->revision != context.revision) {
        return ArtifactBuildResult::Failure("view-family input/revision mismatch");
    }
    auto state = std::make_shared<PublishedViewFamilyState>();
    auto root = std::make_shared<RendererStateFragmentArtifact>();
    state->revision = input->revision;
    state->cameraBufferSize = input->cameraBufferSize;
    state->resourceLayoutRevision = input->resourceLayoutRevision;
    state->views = input->views;
    state->retainedResources = input->retainedResources;
    state->cameraTableImage = input->cameraTableImage;
    state->cullingCameraTableImage = input->cullingCameraTableImage;
    for (const auto& dependency : context.dependencies) {
        const auto dependencyRoot = dependency.payload.Get<RendererStateFragmentArtifact>();
        const auto version = dependencyRoot
            ? dependencyRoot->fragment.payload.Get<PublishedGpuBufferVersion>() : nullptr;
        if (!version || !version->resource) continue;
        state->tableVersions.push_back(version);
        root->fragment.resourceHolds.push_back(version);
        root->catalogEntries.insert(root->catalogEntries.end(),
            dependencyRoot->catalogEntries.begin(), dependencyRoot->catalogEntries.end());
    }

    root->kind = PublishedFragmentKind::Views;
    root->fragment.revision = context.revision;
    root->fragment.payload = ArtifactPayload::Make<PublishedViewFamilyState>(state);
    root->fragment.resourceHolds.reserve(state->retainedResources.size());
    for (const auto& resource : state->retainedResources) root->fragment.resourceHolds.push_back(resource);
    return ArtifactBuildResult::Ready(
        ArtifactPayload::Make<RendererStateFragmentArtifact>(std::move(root)));
}

} // namespace

void RegisterViewStateProducer(AsyncStateGraph& graph) {
    graph.RegisterProducer(ArtifactKind::ViewFamily, {
        TaskLane::FrameCritical, TaskDomain::GraphPublication,
        "ViewFamilyArtifact::Build", BuildViewFamily });
}

} // namespace br::render
