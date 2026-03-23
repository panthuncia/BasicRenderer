#include "Render/GraphExtensions/CLodExtension.h"

#include <cmath>
#include <memory>

#include "Managers/Singletons/RendererECSManager.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/GraphExtensions/ClusterLOD/CLodStreamingSystem.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/ClusterSoftwareRasterizationPass.h"
#include "Render/GraphExtensions/ClusterLOD/HierarchialCullingPass.h"
#include "Render/GraphExtensions/ClusterLOD/PerViewLinearDepthCopyPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockOffsetsPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockScanPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketCompactAndArgsPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketCreateCommandPass.h"
#include "Render/GraphExtensions/ClusterLOD/RasterBucketHistogramPass.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "RenderPasses/FidelityFX/Downsample.h"
#include "Resources/components.h"
#include "Managers/MeshManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Resources/Buffers/PagePool.h"
#include "ShaderBuffers.h"

CLodExtension::~CLodExtension() = default;

CLodExtension::CLodExtension(CLodExtensionType type, uint32_t maxVisibleClusters)
    : m_type(type)
    , m_maxVisibleClusters(maxVisibleClusters)
    , m_streamingSystem(std::make_unique<CLodStreamingSystem>()) {
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    switch (type) {
    case CLodExtensionType::VisiblityBuffer:
        if (!ecsWorld.component<CLodExtensionVisibilityBufferTag>().has(flecs::Exclusive)) {
            ecsWorld.component<CLodExtensionVisibilityBufferTag>().add(flecs::Exclusive);
            ecsWorld.add<CLodExtensionVisibilityBufferTag>();
        }
        break;
    case CLodExtensionType::Shadow:
        if (!ecsWorld.component<CLodExtensionShadowTag>().has(flecs::Exclusive)) {
            ecsWorld.component<CLodExtensionShadowTag>().add(flecs::Exclusive);
            ecsWorld.add<CLodExtensionShadowTag>();
        }
        break;
    }

    m_visibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false);
    m_visibleClustersBuffer->SetName("CLod Visible Clusters Buffer (uncompacted)");
    m_histogramIndirectCommand = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterBucketsHistogramIndirectCommand), true, false);
    m_histogramIndirectCommand->SetName("CLod Raster Buckets Histogram Indirect Command Buffer");
    m_rasterBucketsHistogramBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBuffer->SetName("Raster bucket histogram");

    flecs::entity typeEntity;
    switch (type) {
    case CLodExtensionType::VisiblityBuffer:
        typeEntity = ecsWorld.entity<CLodExtensionVisibilityBufferTag>();
        break;
    case CLodExtensionType::Shadow:
        typeEntity = ecsWorld.entity<CLodExtensionShadowTag>();
        break;
    }

    m_visibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_visibleClustersCounterBuffer->SetName("CLod Visible Clusters Counter Buffer");
    m_visibleClustersCounterBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersCounterBuffer })
        .add<VisibleClustersCounterTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_workGraphTelemetryBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodWorkGraphCounterCount, sizeof(uint32_t), true, false, false, false);
    m_workGraphTelemetryBuffer->SetName("CLod Work Graph Telemetry Buffer");
    m_workGraphTelemetryBuffer->GetECSEntity()
        .set<Components::Resource>({ m_workGraphTelemetryBuffer })
        .add<CLodWorkGraphTelemetryBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_occlusionReplayBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodReplayBufferNumUints, sizeof(uint32_t), true, false, false, false);
    m_occlusionReplayBuffer->SetName("CLod Occlusion Replay Buffer");
    m_occlusionReplayStateBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodReplayBufferState), true, false, false, false);
    m_occlusionReplayStateBuffer->SetName("CLod Occlusion Replay State Buffer");
    m_occlusionNodeGpuInputsBuffer = CreateAliasedUnmaterializedStructuredBuffer(3, sizeof(CLodNodeGpuInput), true, false, false, false);
    m_occlusionNodeGpuInputsBuffer->SetName("CLod Occlusion Node GPU Inputs Buffer");
    m_viewDepthSrvIndicesBuffer = CreateAliasedUnmaterializedStructuredBuffer(CLodMaxViewDepthIndices, sizeof(CLodViewDepthSRVIndex), true, false, false, false);
    m_viewDepthSrvIndicesBuffer->SetName("CLod View Depth SRV Indices Buffer");

    m_rasterBucketsOffsetsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsOffsetsBuffer->SetName("CLod Raster bucket offsets");
    m_rasterBucketsBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsBlockSumsBuffer->SetName("CLod Raster bucket block sums");
    m_rasterBucketsScannedBlockSumsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsScannedBlockSumsBuffer->SetName("CLod Raster bucket scanned block sums");
    m_rasterBucketsTotalCountBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false);
    m_rasterBucketsTotalCountBuffer->SetName("CLod Raster bucket total count");

    m_compactedVisibleClustersBuffer = CreateAliasedUnmaterializedRawBuffer(maxVisibleClusters * PackedVisibleClusterStrideBytes, true, false);
    m_compactedVisibleClustersBuffer->SetName("CLod Compacted Visible Clusters Buffer");

    m_visibleClustersBuffer->GetECSEntity()
        .set<Components::Resource>({ m_visibleClustersBuffer })
        .add<VisibleClustersBufferTag>()
        .add<CLodExtensionTypeTag>(typeEntity);

    m_rasterBucketsWriteCursorBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBuffer->SetName("CLod Raster bucket write cursor");
    m_rasterBucketsIndirectArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(RasterizeClustersCommand), true, false);
    m_rasterBucketsIndirectArgsBuffer->SetName("CLod Raster bucket indirect args");

    m_visibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_visibleClustersCounterBufferPhase2->SetName("CLod Visible Clusters Counter Buffer Phase2");

    m_rasterBucketsHistogramBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferPhase2->SetName("Raster bucket histogram phase2");

    m_rasterBucketsHistogramBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferSw->SetName("Raster bucket histogram SW phase1");

    m_rasterBucketsHistogramBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsHistogramBufferPhase2Sw->SetName("Raster bucket histogram SW phase2");

    m_rasterBucketsWriteCursorBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferPhase2->SetName("CLod Raster bucket write cursor phase2");

    m_rasterBucketsWriteCursorBufferSw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferSw->SetName("CLod Raster bucket write cursor SW phase1");

    m_rasterBucketsWriteCursorBufferPhase2Sw = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(uint32_t), true, false, false, false);
    m_rasterBucketsWriteCursorBufferPhase2Sw->SetName("CLod Raster bucket write cursor SW phase2");

    m_swVisibleClustersCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_swVisibleClustersCounterBuffer->SetName("CLod SW Visible Clusters Counter Buffer Phase1");

    m_swVisibleClustersCounterBufferPhase2 = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(unsigned int), true, false, false, false);
    m_swVisibleClustersCounterBufferPhase2->SetName("CLod SW Visible Clusters Counter Buffer Phase2");

    m_sortedToUnsortedMappingBuffer = CreateAliasedUnmaterializedStructuredBuffer(static_cast<uint32_t>(maxVisibleClusters), sizeof(uint32_t), true, false);
    m_sortedToUnsortedMappingBuffer->SetName("CLod Sorted-to-Unsorted Mapping Buffer");

    m_viewRasterInfoBuffer = CreateAliasedUnmaterializedStructuredBuffer(1, sizeof(CLodViewRasterInfo), false, false, false, false);
    m_viewRasterInfoBuffer->SetName("CLod View Raster Info Buffer");
}

void CLodExtension::Initialize(RenderGraph& rg) {
    if (m_streamingSystem) {
        m_streamingSystem->Initialize(rg);
    }
}

void CLodExtension::OnRegistryReset(ResourceRegistry* reg) {
    if (m_streamingSystem) {
        m_streamingSystem->OnRegistryReset(reg);
    }
}

void CLodExtension::GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) {
    if (m_streamingSystem) {
        m_streamingSystem->GatherStructuralPasses(rg, outPasses);
    }

    const auto softwareRasterMode = SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)();
    const bool useComputeSWRaster = CLodSoftwareRasterUsesCompute(softwareRasterMode);

    // Retrieve the page pool slab ResourceGroup for render graph tracking.
    std::shared_ptr<ResourceGroup> slabGroup;
    try {
        auto getter = SettingsManager::GetInstance().getSettingGetter<std::function<MeshManager*()>>(CLodStreamingMeshManagerGetterSettingName);
        if (auto* mm = getter()()) {
            if (auto* pool = mm->GetCLodPagePool()) {
                slabGroup = pool->GetSlabResourceGroup();
            }
        }
    } catch (...) {}

    RenderGraph::ExternalPassDesc cullPassDesc;
    cullPassDesc.type = RenderGraph::PassType::Compute;
	cullPassDesc.computeQueueSelection = ComputeQueueSelection::Graphics;
    cullPassDesc.name = "CLod::HierarchialCullingPass1";
    HierarchialCullingPassInputs cullPassInputs;
    cullPassInputs.isFirstPass = true;
    cullPassInputs.maxVisibleClusters = m_maxVisibleClusters;
    switch (softwareRasterMode) {
    case CLodSoftwareRasterMode::Disabled:
        cullPassInputs.workGraphMode = HierarchialCullingWorkGraphMode::HardwareOnly;
        break;
    case CLodSoftwareRasterMode::Compute:
        cullPassInputs.workGraphMode = HierarchialCullingWorkGraphMode::SoftwareRasterCompute;
        break;
    case CLodSoftwareRasterMode::WorkGraph:
        cullPassInputs.workGraphMode = HierarchialCullingWorkGraphMode::SoftwareRasterWorkGraph;
        break;
    }
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
    cullPassDesc.where = RenderGraph::ExternalInsertPoint::After("CLod::StreamingBeginFramePass");
    outPasses.push_back(std::move(cullPassDesc));

    RenderGraph::ExternalPassDesc histogramPassDesc;
    histogramPassDesc.type = RenderGraph::PassType::Compute;
    histogramPassDesc.name = "CLod::RasterBucketsHistogramPass1";
    histogramPassDesc.pass = std::make_shared<RasterBucketHistogramPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBuffer,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBuffer);
    outPasses.push_back(std::move(histogramPassDesc));

    RenderGraph::ExternalPassDesc prefixScanPassDesc;
    prefixScanPassDesc.type = RenderGraph::PassType::Compute;
    prefixScanPassDesc.name = "CLod::RasterBucketsPrefixScanPass1";
    prefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
        m_rasterBucketsHistogramBuffer,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer);
    outPasses.push_back(std::move(prefixScanPassDesc));

    RenderGraph::ExternalPassDesc prefixOffsetsPassDesc;
    prefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
    prefixOffsetsPassDesc.name = "CLod::RasterBucketsPrefixOffsetsPass1";
    prefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer,
        m_rasterBucketsScannedBlockSumsBuffer,
        m_rasterBucketsTotalCountBuffer);
    outPasses.push_back(std::move(prefixOffsetsPassDesc));

    RenderGraph::ExternalPassDesc compactPassDesc;
    compactPassDesc.type = RenderGraph::PassType::Compute;
    compactPassDesc.name = "CLod::RasterBucketsCompactAndArgsPass1";
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

    RenderGraph::ExternalPassDesc rasterizePassDesc;
    rasterizePassDesc.type = RenderGraph::PassType::Render;
    rasterizePassDesc.name = "CLod::RasterizeClustersPass1";
    ClusterRasterizationPassInputs rasterizePassInputs;
    rasterizePassInputs.clearGbuffer = true;
    rasterizePassInputs.wireframe = false;
    rasterizePassDesc.pass = std::make_shared<ClusterRasterizationPass>(
        rasterizePassInputs,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsHistogramBuffer,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        slabGroup);
    rasterizePassDesc.isGeometryPass = true;
    outPasses.push_back(std::move(rasterizePassDesc));

    if (useComputeSWRaster) {
        RenderGraph::ExternalPassDesc swCreateCommandPassDesc;
        swCreateCommandPassDesc.type = RenderGraph::PassType::Compute;
        swCreateCommandPassDesc.name = "CLod::RasterBucketsCreateCommandPassSW1";
        swCreateCommandPassDesc.pass = std::make_shared<RasterBucketCreateCommandPass>(
            m_swVisibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            true);
        outPasses.push_back(std::move(swCreateCommandPassDesc));

        RenderGraph::ExternalPassDesc swHistogramPassDesc;
        swHistogramPassDesc.type = RenderGraph::PassType::Compute;
        swHistogramPassDesc.name = "CLod::RasterBucketsHistogramPassSW1";
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
        swPrefixScanPassDesc.name = "CLod::RasterBucketsPrefixScanPassSW1";
        swPrefixScanPassDesc.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBufferSw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            true);
        outPasses.push_back(std::move(swPrefixScanPassDesc));

        RenderGraph::ExternalPassDesc swPrefixOffsetsPassDesc;
        swPrefixOffsetsPassDesc.type = RenderGraph::PassType::Compute;
        swPrefixOffsetsPassDesc.name = "CLod::RasterBucketsPrefixOffsetsPassSW1";
        swPrefixOffsetsPassDesc.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer,
            true);
        outPasses.push_back(std::move(swPrefixOffsetsPassDesc));

        RenderGraph::ExternalPassDesc swCompactPassDesc;
        swCompactPassDesc.type = RenderGraph::PassType::Compute;
        swCompactPassDesc.name = "CLod::RasterBucketsCompactAndArgsPassSW1";
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
        swRasterizePassDesc.name = "CLod::SoftwareRasterizeClustersPass1";
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

    if (m_type != CLodExtensionType::Shadow) {
        RenderGraph::ExternalPassDesc depthCopyPassDesc;
        depthCopyPassDesc.type = RenderGraph::PassType::Compute;
        depthCopyPassDesc.name = "CLod::LinearDepthCopyPass1";
        depthCopyPassDesc.pass = std::make_shared<PerViewLinearDepthCopyPass>();
        outPasses.push_back(std::move(depthCopyPassDesc));
    }

    RenderGraph::ExternalPassDesc downsamplePassDesc;
    downsamplePassDesc.type = RenderGraph::PassType::Compute;
    downsamplePassDesc.name = "CLod::LinearDepthDownsamplePass1";
    downsamplePassDesc.pass = std::make_shared<DownsamplePass>();
    outPasses.push_back(std::move(downsamplePassDesc));

    RenderGraph::ExternalPassDesc cullPassDesc2;
    cullPassDesc2.type = RenderGraph::PassType::Compute;
	cullPassDesc2.computeQueueSelection = ComputeQueueSelection::Graphics;
    cullPassDesc2.name = "CLod::HierarchialCullingPass2";
    HierarchialCullingPassInputs cullPassInputs2;
    cullPassInputs2.isFirstPass = false;
    cullPassInputs2.maxVisibleClusters = m_maxVisibleClusters;
    switch (softwareRasterMode) {
    case CLodSoftwareRasterMode::Disabled:
        cullPassInputs2.workGraphMode = HierarchialCullingWorkGraphMode::HardwareOnly;
        break;
    case CLodSoftwareRasterMode::Compute:
        cullPassInputs2.workGraphMode = HierarchialCullingWorkGraphMode::SoftwareRasterCompute;
        break;
    case CLodSoftwareRasterMode::WorkGraph:
        cullPassInputs2.workGraphMode = HierarchialCullingWorkGraphMode::SoftwareRasterWorkGraph;
        break;
    }
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
        m_swVisibleClustersCounterBuffer);  // Phase 2 SW writes must stay disjoint from Phase 1 because the visibility buffer stores unsorted indices.
    outPasses.push_back(std::move(cullPassDesc2));

    RenderGraph::ExternalPassDesc histogramPassDesc2;
    histogramPassDesc2.type = RenderGraph::PassType::Compute;
    histogramPassDesc2.name = "CLod::RasterBucketsHistogramPass2";
    histogramPassDesc2.pass = std::make_shared<RasterBucketHistogramPass>(
        m_visibleClustersBuffer,
        m_visibleClustersCounterBufferPhase2,
        m_histogramIndirectCommand,
        m_rasterBucketsHistogramBufferPhase2,
        m_visibleClustersCounterBuffer);  // Phase 1's HW counter for read offset
    outPasses.push_back(std::move(histogramPassDesc2));

    RenderGraph::ExternalPassDesc prefixScanPassDesc2;
    prefixScanPassDesc2.type = RenderGraph::PassType::Compute;
    prefixScanPassDesc2.name = "CLod::RasterBucketsPrefixScanPass2";
    prefixScanPassDesc2.pass = std::make_shared<RasterBucketBlockScanPass>(
        m_rasterBucketsHistogramBufferPhase2,
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer);
    outPasses.push_back(std::move(prefixScanPassDesc2));

    RenderGraph::ExternalPassDesc prefixOffsetsPassDesc2;
    prefixOffsetsPassDesc2.type = RenderGraph::PassType::Compute;
    prefixOffsetsPassDesc2.name = "CLod::RasterBucketsPrefixOffsetsPass2";
    prefixOffsetsPassDesc2.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
        m_rasterBucketsOffsetsBuffer,
        m_rasterBucketsBlockSumsBuffer,
        m_rasterBucketsScannedBlockSumsBuffer,
        m_rasterBucketsTotalCountBuffer);
    outPasses.push_back(std::move(prefixOffsetsPassDesc2));

    RenderGraph::ExternalPassDesc compactPassDesc2;
    compactPassDesc2.type = RenderGraph::PassType::Compute;
    compactPassDesc2.name = "CLod::RasterBucketsCompactAndArgsPass2";
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
    rasterizePassDesc2.name = "CLod::RasterizeClustersPass2";
    rasterizePassDesc2.where = RenderGraph::ExternalInsertPoint::Before("MaterialHistogramPass");
    ClusterRasterizationPassInputs rasterizePassInputs2;
    rasterizePassInputs2.clearGbuffer = false;
    rasterizePassInputs2.wireframe = false;
    rasterizePassDesc2.pass = std::make_shared<ClusterRasterizationPass>(
        rasterizePassInputs2,
        m_compactedVisibleClustersBuffer,
        m_rasterBucketsHistogramBufferPhase2,
        m_rasterBucketsIndirectArgsBuffer,
        m_sortedToUnsortedMappingBuffer,
        slabGroup);
    rasterizePassDesc2.isGeometryPass = true;
    outPasses.push_back(std::move(rasterizePassDesc2));

    if (useComputeSWRaster) {
        RenderGraph::ExternalPassDesc swCreateCommandPassDesc2;
        swCreateCommandPassDesc2.type = RenderGraph::PassType::Compute;
        swCreateCommandPassDesc2.name = "CLod::RasterBucketsCreateCommandPassSW2";
        swCreateCommandPassDesc2.pass = std::make_shared<RasterBucketCreateCommandPass>(
            m_swVisibleClustersCounterBufferPhase2,
            m_histogramIndirectCommand,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            true);
        outPasses.push_back(std::move(swCreateCommandPassDesc2));

        RenderGraph::ExternalPassDesc swHistogramPassDesc2;
        swHistogramPassDesc2.type = RenderGraph::PassType::Compute;
        swHistogramPassDesc2.name = "CLod::RasterBucketsHistogramPassSW2";
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
        swPrefixScanPassDesc2.name = "CLod::RasterBucketsPrefixScanPassSW2";
        swPrefixScanPassDesc2.pass = std::make_shared<RasterBucketBlockScanPass>(
            m_rasterBucketsHistogramBufferPhase2Sw,
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            true);
        outPasses.push_back(std::move(swPrefixScanPassDesc2));

        RenderGraph::ExternalPassDesc swPrefixOffsetsPassDesc2;
        swPrefixOffsetsPassDesc2.type = RenderGraph::PassType::Compute;
        swPrefixOffsetsPassDesc2.name = "CLod::RasterBucketsPrefixOffsetsPassSW2";
        swPrefixOffsetsPassDesc2.pass = std::make_shared<RasterBucketBlockOffsetsPass>(
            m_rasterBucketsOffsetsBuffer,
            m_rasterBucketsBlockSumsBuffer,
            m_rasterBucketsScannedBlockSumsBuffer,
            m_rasterBucketsTotalCountBuffer,
            true);
        outPasses.push_back(std::move(swPrefixOffsetsPassDesc2));

        RenderGraph::ExternalPassDesc swCompactPassDesc2;
        swCompactPassDesc2.type = RenderGraph::PassType::Compute;
        swCompactPassDesc2.name = "CLod::RasterBucketsCompactAndArgsPassSW2";
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
        swRasterizePassDesc2.name = "CLod::SoftwareRasterizeClustersPass2";
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

    if (m_type != CLodExtensionType::Shadow) {
        RenderGraph::ExternalPassDesc depthCopyPassDesc2;
        depthCopyPassDesc2.type = RenderGraph::PassType::Compute;
        depthCopyPassDesc2.name = "CLod::LinearDepthCopyPass2";
        depthCopyPassDesc2.where = RenderGraph::ExternalInsertPoint::Before("DeferredShadingPass");
        depthCopyPassDesc2.pass = std::make_shared<PerViewLinearDepthCopyPass>();
        outPasses.push_back(std::move(depthCopyPassDesc2));
    }

    RenderGraph::ExternalPassDesc downsamplePassDesc2;
    downsamplePassDesc2.type = RenderGraph::PassType::Compute;
    downsamplePassDesc2.name = "CLod::LinearDepthDownsamplePass2";
    downsamplePassDesc2.pass = std::make_shared<DownsamplePass>();
    outPasses.push_back(std::move(downsamplePassDesc2));
}

void CLodExtension::GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& outPasses) {
    if (m_streamingSystem) {
       m_streamingSystem->GatherFramePasses(rg, outPasses);
    }
}
