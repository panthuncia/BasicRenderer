#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "Managers/ViewManager.h"
#include "Render/PublishedRendererState.h"
#include "Render/IndirectStateArtifacts.h"
#include "Render/RenderContext.h"
#include "Render/RenderPhase.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"

namespace br::render {

inline ViewFilter PreparedCullViewFilter(bool useShadowCascadeViews) {
    if (!useShadowCascadeViews) return ViewFilter::PrimaryCameras();
    auto filter = ViewFilter::Shadows();
    filter.requireCascade = true;
    filter.requireLightType = true;
    filter.lightType = Components::LightType::Directional;
    return filter;
}

// Shared, owned input for both hierarchical culling encoders. Building this
// list is preparation work: recording workers never enumerate ViewManager or
// consult a live publication source.
struct PreparedCullingWorkload {
    uint32_t viewDataIndex = 0;
    uint32_t activeDrawSetIndicesSRVIndex = 0;
    uint32_t activeDrawCount = 0;
    uint32_t drawRecordVisibilityGenerationSRVIndex = 0;
    uint32_t shadowCasterClass = 0;
    uint32_t dispatchGridX = 0;
    uint32_t dispatchGridY = 1;
    uint32_t dispatchGridZ = 1;
};

inline std::vector<PreparedCullingWorkload> PrepareCullingWorkloads(
    std::span<const PreparedViewFrameData> views,
    const std::shared_ptr<const PublishedRendererState>& rendererState,
    RenderPhase renderPhase,
    bool clodOnlyWorkloads,
    bool useShadowCascadeViews,
    CLodRasterOutputKind rasterOutputKind,
    uint32_t threadsPerGroup,
    std::string_view diagnosticName) {
    std::vector<PreparedCullingWorkload> result;
    const ViewFilter filter = PreparedCullViewFilter(useShadowCascadeViews);
    const auto published = rendererState
        ? rendererState->indirectWorkloads.payload.Get<PublishedIndirectState>()
        : nullptr;

    for (const auto& view : views) {
        if ((filter.requirePrimary && !view.primary)
            || (filter.requireShadow && !view.shadow)
            || (filter.requireCascade && !view.cascade)
            || (filter.requireLightType && view.lightType != filter.lightType)) continue;

        const auto workloads = published
            ? published->Find(view.id, renderPhase, clodOnlyWorkloads)
            : std::vector<const PublishedIndirectWorkload*>{};
        if (!workloads.empty() && !published->visibilityGenerations) {
            spdlog::error("{}: skipping indirect snapshot without its exact visibility-generation resource", diagnosticName);
            continue;
        }
        for (const auto* workload : workloads) {
            const auto activeDrawList = workload ? workload->activeDrawList : nullptr;
            if (!activeDrawList) {
                spdlog::warn("{}: skipping stale workload without active draw set indices", diagnosticName);
                continue;
            }
            if (workload->count == 0) continue;

            result.push_back({
                .viewDataIndex = view.cameraBufferIndex,
                .activeDrawSetIndicesSRVIndex = workload->activeDrawListSRVIndex,
                .activeDrawCount = workload->count,
                .drawRecordVisibilityGenerationSRVIndex = published->visibilityGenerationsSRVIndex,
                .shadowCasterClass = rasterOutputKind == CLodRasterOutputKind::VirtualShadow
                    ? (workload->key.skinnedShadowCaster ? 2u : 1u)
                    : 0u,
                .dispatchGridX = (workload->count + threadsPerGroup - 1u) / threadsPerGroup,
            });
        }
    }
    return result;
}

} // namespace br::render
