#include "Render/GraphExtensions/CLodExtension.h"

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Managers/MeshManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/GraphExtensions/ClusterLOD/ClearDeepVisibilityPass.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingSystem.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterSoftwareRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/DeepVisibilityResolvePass.h"
#include "Render/GraphExtensions/ClusterLOD/HierarchialCullingPass.h"
#include "Render/GraphExtensions/ClusterLOD/PerViewLinearDepthCopyPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockOffsetsPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockScanPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketCompactAndArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketCreateCommandPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketHistogramPass.h"
#include "Render/RenderPhase.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "Resources/Buffers/PagePool.h"
#include "Resources/components.h"
#include "ShaderBuffers.h"

namespace {

flecs::entity EnsureVisibilityBufferTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionVisibilityBufferTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionVisibilityBufferTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionVisibilityBufferTag>();
    }
    return ecsWorld.entity<CLodExtensionVisibilityBufferTag>();
}

flecs::entity EnsureAlphaBlendTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionAlphaBlendTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionAlphaBlendTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionAlphaBlendTag>();
    }
    return ecsWorld.entity<CLodExtensionAlphaBlendTag>();
}

flecs::entity EnsureShadowTag(flecs::world& ecsWorld)
{
    if (!ecsWorld.component<CLodExtensionShadowTag>().has(flecs::Exclusive)) {
        ecsWorld.component<CLodExtensionShadowTag>().add(flecs::Exclusive);
        ecsWorld.add<CLodExtensionShadowTag>();
    }
    return ecsWorld.entity<CLodExtensionShadowTag>();
}

struct CLodVariantTraits {
    enum class ScheduleMode : uint8_t {
        TwoPassVisibility,
        SinglePassCullOnly,
        SinglePassDeepVisibility
    };

    CLodExtensionType type;
    flecs::entity (*ensureTypeEntity)(flecs::world&);
    std::string_view passPrefix;
    std::string_view resourcePrefix;
    std::string_view renderPhaseName;
    CLodRasterOutputKind rasterOutputKind;
    ScheduleMode scheduleMode = ScheduleMode::TwoPassVisibility;
    bool ownsStreaming = false;
    bool usesPhase2OcclusionReplay = false;
    bool schedulesPerViewDepthCopy = false;
    bool schedulesStructuralPasses = false;
};

const CLodVariantTraits& GetVariantTraits(CLodExtensionType type)
{
    static const std::array<CLodVariantTraits, 3> kTraits = {{
        {
            CLodExtensionType::VisiblityBuffer,
            &EnsureVisibilityBufferTag,
            "CLodOpaque::",
            "CLod[Opaque] ",
            Engine::Primary::GBufferPass,
            CLodRasterOutputKind::VisibilityBuffer,
            CLodVariantTraits::ScheduleMode::TwoPassVisibility,
            true,
            true,
            true,
            true
        },
        {
            CLodExtensionType::AlphaBlend,
            &EnsureAlphaBlendTag,
            "CLodAlpha::",
            "CLod[Alpha] ",
            Engine::Primary::CLodTransparentPass,
            CLodRasterOutputKind::DeepVisibility,
            CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility,
            false,
            false,
            false,
            true
        },
        {
            CLodExtensionType::Shadow,
            &EnsureShadowTag,
            "CLodShadow::",
            "CLod[Shadow] ",
            Engine::Primary::GBufferPass,
            CLodRasterOutputKind::VisibilityBuffer,
            CLodVariantTraits::ScheduleMode::TwoPassVisibility,
            false,
            true,
            false,
            true
        },
    }};

    for (const auto& traits : kTraits) {
        if (traits.type == type) {
            return traits;
        }
    }

    throw std::runtime_error("Unknown CLod extension type.");
}

std::string MakeVariantPassName(const CLodVariantTraits& traits, std::string_view suffix)
{
    return std::string(traits.passPrefix) + std::string(suffix);
}

std::string MakeVariantResourceName(const CLodVariantTraits& traits, std::string_view suffix)
{
    return std::string(traits.resourcePrefix) + std::string(suffix);
}

HierarchialCullingWorkGraphMode GetCullingWorkGraphMode(CLodSoftwareRasterMode softwareRasterMode)
{
    switch (softwareRasterMode) {
    case CLodSoftwareRasterMode::Disabled:
        return HierarchialCullingWorkGraphMode::HardwareOnly;
    case CLodSoftwareRasterMode::Compute:
        return HierarchialCullingWorkGraphMode::SoftwareRasterCompute;
    case CLodSoftwareRasterMode::WorkGraph:
        return HierarchialCullingWorkGraphMode::SoftwareRasterWorkGraph;
    }

    return HierarchialCullingWorkGraphMode::HardwareOnly;
}

std::shared_ptr<ResourceGroup> GetSlabResourceGroup()
{
    try {
        auto getter = SettingsManager::GetInstance().getSettingGetter<std::function<MeshManager*()>>(CLodStreamingMeshManagerGetterSettingName);
        if (auto* meshManager = getter()()) {
            if (auto* pool = meshManager->GetCLodPagePool()) {
                return pool->GetSlabResourceGroup();
            }
        }
    } catch (...) {}

    return nullptr;
}

} // namespace

CLodExtension::~CLodExtension() = default;

CLodExtension::CLodExtension(CLodExtensionType type, uint32_t maxVisibleClusters)
    : m_type(type)
    , m_maxVisibleClusters(maxVisibleClusters) {
    const auto& traits = GetVariantTraits(type);
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    const flecs::entity typeEntity = traits.ensureTypeEntity(ecsWorld);

    if (traits.ownsStreaming) {
        m_streamingSystem = std::make_unique<CLodStreamingSystem>();
    }

    m_visibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false);
    m_visibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Visible Clusters Buffer (uncompacted)"));

    m_histogramIndirectCommand = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterBucketsHistogramIndirectCommand), true, false);
    m_histogramIndirectCommand->SetName(MakeVariantResourceName(traits, "Raster Buckets Histogram Indirect Command Buffer"));

    m_rasterBucketsHistogramBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket histogram"));

    m_visibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_visibleClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "Visible Clusters Counter Buffer"));
    m_visibleClustersCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersCounterBuffer })
        .add<VisibleClustersCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_workGraphTelemetryBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodWorkGraphCounterCount, sizeof(uint32_t), true, false, false, false);
    m_workGraphTelemetryBuffer->SetName(MakeVariantResourceName(traits, "Work Graph Telemetry Buffer"));
    m_workGraphTelemetryBuffer->GetECSEntity()
        .set<Components::Resource>({ m_workGraphTelemetryBuffer })
        .add<CLodWorkGraphTelemetryBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_occlusionReplayBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodReplayBufferNumUints, sizeof(uint32_t), true, false, false, false);
    m_occlusionReplayBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Replay Buffer"));

    m_occlusionReplayStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReplayBufferState), true, false, false, false);
    m_occlusionReplayStateBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Replay State Buffer"));

    m_occlusionNodeGpuInputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(3, sizeof(CLodNodeGpuInput), true, false, false, false);
    m_occlusionNodeGpuInputsBuffer->SetName(MakeVariantResourceName(traits, "Occlusion Node GPU Inputs Buffer"));

    m_viewDepthSrvIndicesBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodMaxViewDepthIndices, sizeof(CLodViewDepthSRVIndex), true, false, false, false);
    m_viewDepthSrvIndicesBuffer->SetName(MakeVariantResourceName(traits, "View Depth SRV Indices Buffer"));

    m_rasterBucketsOffsetsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsOffsetsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket offsets"));

    m_rasterBucketsBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsBlockSumsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket block sums"));

    m_rasterBucketsScannedBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsScannedBlockSumsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket scanned block sums"));

    m_rasterBucketsTotalCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsTotalCountBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket total count"));

    m_compactedVisibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false);
    m_compactedVisibleClustersBuffer->SetName(MakeVariantResourceName(traits, "Compacted Visible Clusters Buffer"));

    m_visibleClustersBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersBuffer })
        .add<VisibleClustersBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_rasterBucketsWriteCursorBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor"));

    m_rasterBucketsIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false);
    m_rasterBucketsIndirectArgsBuffer->SetName(MakeVariantResourceName(traits, "Raster bucket indirect args"));

    m_visibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_visibleClustersCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "Visible Clusters Counter Buffer Phase2"));

    m_rasterBucketsHistogramBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferPhase2->SetName(MakeVariantResourceName(traits, "Raster bucket histogram phase2"));

    m_rasterBucketsHistogramBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferSw->SetName(MakeVariantResourceName(traits, "Raster bucket histogram SW phase1"));

    m_rasterBucketsHistogramBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferPhase2Sw->SetName(MakeVariantResourceName(traits, "Raster bucket histogram SW phase2"));

    m_rasterBucketsWriteCursorBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferPhase2->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor phase2"));

    m_rasterBucketsWriteCursorBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferSw->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor SW phase1"));

    m_rasterBucketsWriteCursorBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferPhase2Sw->SetName(MakeVariantResourceName(traits, "Raster bucket write cursor SW phase2"));

    m_swVisibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_swVisibleClustersCounterBuffer->SetName(MakeVariantResourceName(traits, "SW Visible Clusters Counter Buffer Phase1"));

    m_swVisibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_swVisibleClustersCounterBufferPhase2->SetName(MakeVariantResourceName(traits, "SW Visible Clusters Counter Buffer Phase2"));

    m_sortedToUnsortedMappingBuffer = CreateAliasedUnmaterializedStructuredBuffer(static_cast<uint32_t>(maxVisibleClusters), sizeof(uint32_t), true, false);
    m_sortedToUnsortedMappingBuffer->SetName(MakeVariantResourceName(traits, "Sorted-to-Unsorted Mapping Buffer"));

    m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
    m_viewRasterInfoBuffer->SetName(MakeVariantResourceName(traits, "View Raster Info Buffer"));

    if (traits.rasterOutputKind == CLodRasterOutputKind::DeepVisibility) {
        m_deepVisibilityNodesBuffer = CreateAliasedUnmaterializedStructuredBuffer(
            1,
            sizeof(CLodDeepVisibilityNode),
            true,
            false,
            false,
            false);
        m_deepVisibilityNodesBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Nodes Buffer"));

        m_deepVisibilityCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, true, false);
        m_deepVisibilityCounterBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Counter Buffer"));
        m_deepVisibilityCounterBuffer->GetECSEntity()
            .set<Components::Resource>({ m_deepVisibilityCounterBuffer })
            .add<CLodDeepVisibilityCounterTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_deepVisibilityOverflowCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, true, false);
        m_deepVisibilityOverflowCounterBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Overflow Counter Buffer"));
        m_deepVisibilityOverflowCounterBuffer->GetECSEntity()
            .set<Components::Resource>({ m_deepVisibilityOverflowCounterBuffer })
            .add<CLodDeepVisibilityOverflowCounterTag>()
            .add<CLodExtensionTypeTag>(typeEntity);

        m_deepVisibilityStatsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
            1,
            sizeof(CLodDeepVisibilityStats),
            true,
            false,
            true,
            false);
        m_deepVisibilityStatsBuffer->SetName(MakeVariantResourceName(traits, "Deep Visibility Stats Buffer"));
        m_deepVisibilityStatsBuffer->GetECSEntity()
            .set<Components::Resource>({ m_deepVisibilityStatsBuffer })
            .add<CLodDeepVisibilityStatsTag>()
            .add<CLodExtensionTypeTag>(typeEntity);
    }
}

void CLodExtension::Initialize(RenderGraph& rg)
{
    if (m_streamingSystem) {
        m_streamingSystem->Initialize(rg);
    }
}

void CLodExtension::OnRegistryReset(ResourceRegistry* reg)
{
    if (m_streamingSystem) {
        m_streamingSystem->OnRegistryReset(reg);
    }
}

void CLodExtension::GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses)
{
    const auto& traits = GetVariantTraits(m_type);
    if (m_streamingSystem) {
        m_streamingSystem->GatherStructuralPasses(rg, outPasses);
    }

    if (!traits.schedulesStructuralPasses) {
        return;
    }

    const auto softwareRasterMode =
        SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)();
    const bool forceHardwareOnly =
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly ||
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility;
    const bool useComputeSWRaster = !forceHardwareOnly && CLodSoftwareRasterUsesCompute(softwareRasterMode);
    const auto workGraphMode = forceHardwareOnly
        ? HierarchialCullingWorkGraphMode::HardwareOnly
        : GetCullingWorkGraphMode(softwareRasterMode);
    const auto renderPhase = RenderPhase(traits.renderPhaseName.data());

    std::shared_ptr<ResourceGroup> slabGroup = GetSlabResourceGroup();

    RenderGraph::ExternalPassDesc cullPassDesc;
    cullPassDesc.type = RenderGraph::PassType::Compute;
    //cullPassDesc.computeQueueSelection = ComputeQueueSelection::Graphics;
    cullPassDesc.name = MakeVariantPassName(traits, "HierarchialCullingPass1");
    HierarchialCullingPassInputs cullPassInputs;
    cullPassInputs.isFirstPass = true;
    cullPassInputs.maxVisibleClusters = m_maxVisibleClusters;
    cullPassInputs.workGraphMode = workGraphMode;
    cullPassInputs.renderPhase = renderPhase;
    cullPassInputs.clodOnlyWorkloads = true;
    cullPassInputs.rasterOutputKind = traits.rasterOutputKind;
    cullPassDesc.pass = std::make_shared<HierarchialCullingPass>(
        cullPassInputs,
        m_visibleClustersBuffer,
        m_visibleClustersCounterBuffer,
        m_swVisibleClustersCounterBuffer,
        m_histogramIndirectCommand,
        m_workGraphTelemetryBuffer,
        m_occlusionReplayBuffer,
        m_occlusionReplayStateBuffer,
        m_occlusionNodeGpuInputsBuffer,
        m_viewDepthSrvIndicesBuffer,
        m_viewRasterInfoBuffer,
        slabGroup);
    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly ||
        traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility) {
        cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("CLodOpaque::LinearDepthDownsamplePass2");
    }
    else {
        cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("CLod::StreamingBeginFramePass");
    }
    outPasses.push_back(std::move(cullPassDesc));

    RenderGraph::ExternalPassDesc histogramPassDesc;
    histogramPassDesc.type = RenderGraph::PassType::Compute;
    histogramPassDesc.name = MakeVariantPassName(traits, "RasterBucketsHistogramPass1");
    histogramPassDesc.pass = std::make_shared<RasterBucketHistogramPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBuffer,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBuffer);
    outPasses.push_back(std::move(histogramPassDesc));

    RenderGraph::ExternalPassDesc prefixScanPassDesc;
    prefixScanPassDesc.type = RenderGraph::PassType::Compute;
    prefixScanPassDesc.name = MakeVariantPassName(traits, "RasterBucketsPrefixScanPass1");
    prefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
        m_rasterBucketsHistogramBuffer,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer);
    outPasses.push_back(std::move(prefixScanPassDesc));

    RenderGraph::ExternalPassDesc prefixOffsetsPassDesc;
    prefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
    prefixOffsetsPassDesc.name = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPass1");
    prefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer,
        m_rasterBucketsScannedBlockSumsBuffer,
        m_rasterBucketsTotalCountBuffer);
    outPasses.push_back(std::move(prefixOffsetsPassDesc));

    RenderGraph::ExternalPassDesc compactPassDesc;
    compactPassDesc.type = RenderGraph::PassType::Compute;
    compactPassDesc.name = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPass1");
    compactPassDesc.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBuffer,
        m_visibleClustersCounterBuffer,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBuffer,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsWriteCursorBuffer,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        m_maxVisibleClusters,
        false);
    outPasses.push_back(std::move(compactPassDesc));

    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassCullOnly) {
        return;
    }

    if (traits.scheduleMode == CLodVariantTraits::ScheduleMode::SinglePassDeepVisibility) {
        RenderGraph::ExternalPassDesc clearDeepVisibilityPassDesc;
        clearDeepVisibilityPassDesc.type = RenderGraph::PassType::Render;
        clearDeepVisibilityPassDesc.name = MakeVariantPassName(traits, "ClearDeepVisibilityPass");
        clearDeepVisibilityPassDesc.pass = std::make_shared<ClearDeepVisibilityPass>(
            m_deepVisibilityCounterBuffer,
            m_deepVisibilityOverflowCounterBuffer,
            m_deepVisibilityStatsBuffer);
        outPasses.push_back(std::move(clearDeepVisibilityPassDesc));

        RenderGraph::ExternalPassDesc rasterizeDeepVisibilityPassDesc;
        rasterizeDeepVisibilityPassDesc.type = RenderGraph::PassType::Render;
        rasterizeDeepVisibilityPassDesc.name = MakeVariantPassName(traits, "RasterizeClustersPass1");
        ClusterRasterizationPassInputs rasterizePassInputs;
        rasterizePassInputs.clearGbuffer = false;
        rasterizePassInputs.wireframe = false;
        rasterizePassInputs.renderPhase = renderPhase;
        rasterizePassInputs.outputKind = traits.rasterOutputKind;
        rasterizeDeepVisibilityPassDesc.pass = std::make_shared<ClusterRasterizationPass>(
            rasterizePassInputs,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_deepVisibilityNodesBuffer,
            m_deepVisibilityCounterBuffer,
            m_deepVisibilityOverflowCounterBuffer,
            slabGroup);
        rasterizeDeepVisibilityPassDesc.isGeometryPass = true;
        outPasses.push_back(std::move(rasterizeDeepVisibilityPassDesc));

        RenderGraph::ExternalPassDesc resolveDeepVisibilityPassDesc;
        resolveDeepVisibilityPassDesc.type = RenderGraph::PassType::Compute;
        resolveDeepVisibilityPassDesc.name = MakeVariantPassName(traits, "DeepVisibilityResolvePass");
        resolveDeepVisibilityPassDesc.where = RenderGraph::ExternalInsertPoint::Before("PPLLResolvePass");
        resolveDeepVisibilityPassDesc.pass = std::make_shared<DeepVisibilityResolvePass>(
            m_visibleClustersBuffer,
            m_deepVisibilityNodesBuffer,
            m_deepVisibilityCounterBuffer,
            m_deepVisibilityOverflowCounterBuffer,
            m_deepVisibilityStatsBuffer);
        outPasses.push_back(std::move(resolveDeepVisibilityPassDesc));
        return;
    }

    RenderGraph::ExternalPassDesc rasterizePassDesc;
    rasterizePassDesc.type = RenderGraph::PassType::Render;
    rasterizePassDesc.name = MakeVariantPassName(traits, "RasterizeClustersPass1");
    ClusterRasterizationPassInputs rasterizePassInputs;
    rasterizePassInputs.clearGbuffer = true;
    rasterizePassInputs.wireframe = false;
    rasterizePassInputs.renderPhase = renderPhase;
    rasterizePassInputs.outputKind = traits.rasterOutputKind;
    rasterizePassDesc.pass = std::make_shared<ClusterRasterizationPass>(
        rasterizePassInputs,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsHistogramBuffer,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        nullptr,
        nullptr,
        nullptr,
        slabGroup);
    rasterizePassDesc.isGeometryPass = true;
    outPasses.push_back(std::move(rasterizePassDesc));

    if (useComputeSWRaster) {
        RenderGraph::ExternalPassDesc swCreateCommandPassDesc;
        swCreateCommandPassDesc.type = RenderGraph::PassType::Compute;
        swCreateCommandPassDesc.name = MakeVariantPassName(traits, "RasterBucketsCreateCommandPassSW1");
        swCreateCommandPassDesc.pass = std::make_shared<RasterBucketCreateCommandPass>(
            m_swVisibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            true);
        outPasses.push_back(std::move(swCreateCommandPassDesc));

        RenderGraph::ExternalPassDesc swHistogramPassDesc;
        swHistogramPassDesc.type = RenderGraph::PassType::Compute;
        swHistogramPassDesc.name = MakeVariantPassName(traits, "RasterBucketsHistogramPassSW1");
        swHistogramPassDesc.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer,
            m_swVisibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferSw,
            nullptr,
            true,
            m_maxVisibleClusters,
            true);
        outPasses.push_back(std::move(swHistogramPassDesc));

        RenderGraph::ExternalPassDesc swPrefixScanPassDesc;
        swPrefixScanPassDesc.type = RenderGraph::PassType::Compute;
        swPrefixScanPassDesc.name = MakeVariantPassName(traits, "RasterBucketsPrefixScanPassSW1");
        swPrefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBufferSw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            true);
        outPasses.push_back(std::move(swPrefixScanPassDesc));

        RenderGraph::ExternalPassDesc swPrefixOffsetsPassDesc;
        swPrefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
        swPrefixOffsetsPassDesc.name = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassSW1");
        swPrefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer,
            true);
        outPasses.push_back(std::move(swPrefixOffsetsPassDesc));

        RenderGraph::ExternalPassDesc swCompactPassDesc;
        swCompactPassDesc.type = RenderGraph::PassType::Compute;
        swCompactPassDesc.name = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassSW1");
        swCompactPassDesc.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
            m_visibleClustersBuffer,
            m_swVisibleClustersCounterBuffer,
            m_swVisibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferSw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsWriteCursorBufferSw,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_maxVisibleClusters,
            false,
            true,
            true,
            true);
        outPasses.push_back(std::move(swCompactPassDesc));

        RenderGraph::ExternalPassDesc swRasterizePassDesc;
        swRasterizePassDesc.type = RenderGraph::PassType::Compute;
        swRasterizePassDesc.name = MakeVariantPassName(traits, "SoftwareRasterizeClustersPass1");
        swRasterizePassDesc.pass = std::make_shared<ClusterSoftwareRasterizationPass>(
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBufferSw,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_viewRasterInfoBuffer,
            slabGroup,
            true);
        outPasses.push_back(std::move(swRasterizePassDesc));
    }

    if (traits.schedulesPerViewDepthCopy) {
        RenderGraph::ExternalPassDesc depthCopyPassDesc;
        depthCopyPassDesc.type = RenderGraph::PassType::Compute;
        depthCopyPassDesc.name = MakeVariantPassName(traits, "LinearDepthCopyPass1");
        depthCopyPassDesc.pass = std::make_shared<PerViewLinearDepthCopyPass>();
        outPasses.push_back(std::move(depthCopyPassDesc));
    }

    RenderGraph::ExternalPassDesc downsamplePassDesc;
    downsamplePassDesc.type = RenderGraph::PassType::Compute;
    downsamplePassDesc.name = MakeVariantPassName(traits, "LinearDepthDownsamplePass1");
    downsamplePassDesc.pass = std::make_shared<DownsamplePass>();
    outPasses.push_back(std::move(downsamplePassDesc));

    if (!traits.usesPhase2OcclusionReplay) {
        return;
    }

    RenderGraph::ExternalPassDesc cullPassDesc2;
    cullPassDesc2.type = RenderGraph::PassType::Compute;
    //cullPassDesc2.computeQueueSelection = ComputeQueueSelection::Graphics;
    cullPassDesc2.name = MakeVariantPassName(traits, "HierarchialCullingPass2");
    HierarchialCullingPassInputs cullPassInputs2;
    cullPassInputs2.isFirstPass = false;
    cullPassInputs2.maxVisibleClusters = m_maxVisibleClusters;
    cullPassInputs2.workGraphMode = workGraphMode;
    cullPassInputs2.renderPhase = renderPhase;
    cullPassInputs2.clodOnlyWorkloads = true;
    cullPassInputs2.rasterOutputKind = traits.rasterOutputKind;
    cullPassDesc2.pass = std::make_shared<HierarchialCullingPass>(
        cullPassInputs2,
        m_visibleClustersBuffer,
        m_visibleClustersCounterBufferPhase2,
        m_swVisibleClustersCounterBufferPhase2,
        m_histogramIndirectCommand,
        m_workGraphTelemetryBuffer,
        m_occlusionReplayBuffer,
        m_occlusionReplayStateBuffer,
        m_occlusionNodeGpuInputsBuffer,
        m_viewDepthSrvIndicesBuffer,
        m_viewRasterInfoBuffer,
        slabGroup,
        m_visibleClustersCounterBuffer,
        m_swVisibleClustersCounterBuffer);
    outPasses.push_back(std::move(cullPassDesc2));

    RenderGraph::ExternalPassDesc histogramPassDesc2;
    histogramPassDesc2.type = RenderGraph::PassType::Compute;
    histogramPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsHistogramPass2");
    histogramPassDesc2.pass = std::make_shared<RasterBucketHistogramPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBufferPhase2,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBufferPhase2,
        m_visibleClustersCounterBuffer);
    outPasses.push_back(std::move(histogramPassDesc2));

    RenderGraph::ExternalPassDesc prefixScanPassDesc2;
    prefixScanPassDesc2.type = RenderGraph::PassType::Compute;
    prefixScanPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsPrefixScanPass2");
    prefixScanPassDesc2.pass = std::make_shared<RasterBucketBlockScanPass>(
        m_rasterBucketsHistogramBufferPhase2,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer);
    outPasses.push_back(std::move(prefixScanPassDesc2));

    RenderGraph::ExternalPassDesc prefixOffsetsPassDesc2;
    prefixOffsetsPassDesc2.type = RenderGraph::PassType::Compute;
    prefixOffsetsPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPass2");
    prefixOffsetsPassDesc2.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer,
        m_rasterBucketsScannedBlockSumsBuffer,
        m_rasterBucketsTotalCountBuffer);
    outPasses.push_back(std::move(prefixOffsetsPassDesc2));

    RenderGraph::ExternalPassDesc compactPassDesc2;
    compactPassDesc2.type = RenderGraph::PassType::Compute;
    compactPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPass2");
    compactPassDesc2.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBufferPhase2,
        m_visibleClustersCounterBuffer,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBufferPhase2,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsWriteCursorBufferPhase2,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        m_maxVisibleClusters,
        true);
    outPasses.push_back(std::move(compactPassDesc2));

    RenderGraph::ExternalPassDesc rasterizePassDesc2;
    rasterizePassDesc2.type = RenderGraph::PassType::Render;
    rasterizePassDesc2.name = MakeVariantPassName(traits, "RasterizeClustersPass2");
    rasterizePassDesc2.where = RenderGraph::ExternalInsertPoint::Before("MaterialHistogramPass");
    ClusterRasterizationPassInputs rasterizePassInputs2;
    rasterizePassInputs2.clearGbuffer = false;
    rasterizePassInputs2.wireframe = false;
    rasterizePassInputs2.renderPhase = renderPhase;
    rasterizePassInputs2.outputKind = traits.rasterOutputKind;
    rasterizePassDesc2.pass = std::make_shared<ClusterRasterizationPass>(
        rasterizePassInputs2,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsHistogramBufferPhase2,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        nullptr,
        nullptr,
        nullptr,
        slabGroup);
    rasterizePassDesc2.isGeometryPass = true;
    outPasses.push_back(std::move(rasterizePassDesc2));

    if (useComputeSWRaster) {
        RenderGraph::ExternalPassDesc swCreateCommandPassDesc2;
        swCreateCommandPassDesc2.type = RenderGraph::PassType::Compute;
        swCreateCommandPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsCreateCommandPassSW2");
        swCreateCommandPassDesc2.pass = std::make_shared<RasterBucketCreateCommandPass>(
            m_swVisibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            true);
        outPasses.push_back(std::move(swCreateCommandPassDesc2));

        RenderGraph::ExternalPassDesc swHistogramPassDesc2;
        swHistogramPassDesc2.type = RenderGraph::PassType::Compute;
        swHistogramPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsHistogramPassSW2");
        swHistogramPassDesc2.pass = std::make_shared<RasterBucketHistogramPass>(
            m_visibleClustersBuffer,
            m_swVisibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferPhase2Sw,
            m_swVisibleClustersCounterBuffer,
            true,
            m_maxVisibleClusters,
            true);
        outPasses.push_back(std::move(swHistogramPassDesc2));

        RenderGraph::ExternalPassDesc swPrefixScanPassDesc2;
        swPrefixScanPassDesc2.type = RenderGraph::PassType::Compute;
        swPrefixScanPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsPrefixScanPassSW2");
        swPrefixScanPassDesc2.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBufferPhase2Sw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            true);
        outPasses.push_back(std::move(swPrefixScanPassDesc2));

        RenderGraph::ExternalPassDesc swPrefixOffsetsPassDesc2;
        swPrefixOffsetsPassDesc2.type = RenderGraph::PassType::Compute;
        swPrefixOffsetsPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsPrefixOffsetsPassSW2");
        swPrefixOffsetsPassDesc2.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer,
            true);
        outPasses.push_back(std::move(swPrefixOffsetsPassDesc2));

        RenderGraph::ExternalPassDesc swCompactPassDesc2;
        swCompactPassDesc2.type = RenderGraph::PassType::Compute;
        swCompactPassDesc2.name = MakeVariantPassName(traits, "RasterBucketsCompactAndArgsPassSW2");
        swCompactPassDesc2.pass = std::make_shared<RasterBucketCompactAndArgsPass>(
            m_visibleClustersBuffer,
            m_swVisibleClustersCounterBufferPhase2,
            m_swVisibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_rasterBucketsHistogramBufferPhase2Sw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsWriteCursorBufferPhase2Sw,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_maxVisibleClusters,
            true,
            true,
            true,
            true);
        outPasses.push_back(std::move(swCompactPassDesc2));

        RenderGraph::ExternalPassDesc swRasterizePassDesc2;
        swRasterizePassDesc2.type = RenderGraph::PassType::Compute;
        swRasterizePassDesc2.name = MakeVariantPassName(traits, "SoftwareRasterizeClustersPass2");
        swRasterizePassDesc2.pass = std::make_shared<ClusterSoftwareRasterizationPass>(
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBufferPhase2Sw,
            m_rasterBucketsIndirectArgsBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_viewRasterInfoBuffer,
            slabGroup,
            true);
        outPasses.push_back(std::move(swRasterizePassDesc2));
    }

    if (traits.schedulesPerViewDepthCopy) {
        RenderGraph::ExternalPassDesc depthCopyPassDesc2;
        depthCopyPassDesc2.type = RenderGraph::PassType::Compute;
        depthCopyPassDesc2.name = MakeVariantPassName(traits, "LinearDepthCopyPass2");
        depthCopyPassDesc2.where = RenderGraph::ExternalInsertPoint::Before("DeferredShadingPass");
        depthCopyPassDesc2.pass = std::make_shared<PerViewLinearDepthCopyPass>();
        outPasses.push_back(std::move(depthCopyPassDesc2));
    }

    RenderGraph::ExternalPassDesc downsamplePassDesc2;
    downsamplePassDesc2.type = RenderGraph::PassType::Compute;
    downsamplePassDesc2.name = MakeVariantPassName(traits, "LinearDepthDownsamplePass2");
    downsamplePassDesc2.pass = std::make_shared<DownsamplePass>();
    outPasses.push_back(std::move(downsamplePassDesc2));
}

void CLodExtension::GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses)
{
    if (m_streamingSystem) {
        m_streamingSystem->GatherFramePasses(rg, outPasses);
    }
}
