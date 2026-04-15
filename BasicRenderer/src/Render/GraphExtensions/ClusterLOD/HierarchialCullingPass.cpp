#include "Render/GraphExtensions/ClusterLOD/HierarchialCullingPass.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <rhi_feature_info.h>
#include <rhi_interop_dx12.h>
#include <spdlog/spdlog.h>

#include "Managers\IndirectCommandBufferManager.h"
#include "Managers\MaterialManager.h"
#include "Managers\ObjectManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Resources/components.h"
#include "Resources/Resolvers/ECSResourceResolver.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "BuiltinResources.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodClearUintBufferRootConstants.h"
#include "../shaders/PerPassRootConstants/clodCreateCommandRootConstants.h"
#include "../shaders/PerPassRootConstants/clodWorkGraphRootConstants.h"

namespace {

constexpr bool kDisableVirtualShadowDirtyPageCulling = false;

bool UsesSWClassification(HierarchialCullingWorkGraphMode mode)
{
    return mode != HierarchialCullingWorkGraphMode::HardwareOnly;
}

bool UsesWorkGraphSWRaster(HierarchialCullingWorkGraphMode mode)
{
    return mode == HierarchialCullingWorkGraphMode::SoftwareRasterWorkGraph;
}

bool UsesVisibilityBufferOutput(CLodRasterOutputKind outputKind)
{
    return outputKind == CLodRasterOutputKind::VisibilityBuffer;
}

bool UsesVirtualShadowOutput(CLodRasterOutputKind outputKind)
{
    return outputKind == CLodRasterOutputKind::VirtualShadow;
}

bool UsesPerViewDepthMapOcclusion(CLodRasterOutputKind outputKind)
{
    return !UsesVirtualShadowOutput(outputKind);
}

ViewFilter GetCullViewFilter(bool useShadowCascadeViews)
{
    if (!useShadowCascadeViews) {
        return ViewFilter::PrimaryCameras();
    }

    ViewFilter filter = ViewFilter::Shadows();
    filter.requireCascade = true;
    filter.requireLightType = true;
    filter.lightType = Components::LightType::Directional;
    return filter;
}
}

HierarchialCullingPass::HierarchialCullingPass(
    HierarchialCullingPassInputs inputs,
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> visibleClustersCounterBuffer,
    std::shared_ptr<Buffer> swVisibleClustersCounterBuffer,
    std::shared_ptr<Buffer> pageJobVisibleClustersBuffer,
    std::shared_ptr<Buffer> pageJobVisibleClustersCounterBuffer,
    std::shared_ptr<Buffer> histogramIndirectCommand,
    std::shared_ptr<Buffer> workGraphTelemetryBuffer,
    std::shared_ptr<Buffer> occlusionReplayBuffer,
    std::shared_ptr<Buffer> occlusionReplayStateBuffer,
    std::shared_ptr<Buffer> occlusionNodeGpuInputsBuffer,
    std::shared_ptr<Buffer> viewDepthSrvIndicesBuffer,
    std::shared_ptr<Buffer> viewRasterInfoBuffer,
    std::shared_ptr<PixelBuffer> shadowDirtyHierarchyTexture,
    std::shared_ptr<ResourceGroup> slabResourceGroup,
    std::shared_ptr<Buffer> phase1VisibleClustersCounterBuffer,
    std::shared_ptr<Buffer> swWriteBaseCounterBuffer,
    std::shared_ptr<Buffer> shadowPredictiveInvalidationCandidatesBuffer,
    std::shared_ptr<Buffer> shadowPredictiveInvalidationCandidateCountBuffer,
    std::shared_ptr<Buffer> shadowInvalidatedInstancesBitsetBuffer,
    std::shared_ptr<PixelBuffer> shadowPageTableTexture,
    std::shared_ptr<PixelBuffer> shadowPhysicalPagesTexture) {
    m_workGraphMode = inputs.workGraphMode;
    m_rasterOutputKind = inputs.rasterOutputKind;
    m_isFirstPass = inputs.isFirstPass;
    m_workGraphComputePageJobDescriptorResourceId =
        std::string(CLodWorkGraphComputePageJobDescriptorBufferId) + "." + std::to_string(reinterpret_cast<uintptr_t>(this));
    spdlog::info(
        "HierarchialCullingPass::HierarchialCullingPass before CreatePipelines this={} workGraphMode={} rasterOutputKind={} isFirstPass={} resourceId='{}'",
        static_cast<const void*>(this),
        static_cast<int>(m_workGraphMode),
        static_cast<int>(m_rasterOutputKind),
        m_isFirstPass,
        m_workGraphComputePageJobDescriptorResourceId);
    CreatePipelines(
        DeviceManager::GetInstance().GetDevice(),
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_workGraph,
        m_createCommandPipelineState,
        m_clearPipelineState);
    spdlog::info(
        "HierarchialCullingPass::HierarchialCullingPass after CreatePipelines this={} hasWorkGraph={} createCommandPipelineValid={} clearPipelineValid={}",
        static_cast<const void*>(this),
        static_cast<bool>(m_workGraph),
        static_cast<bool>(m_createCommandPipelineState.GetAPIPipelineState()),
        static_cast<bool>(m_clearPipelineState.GetAPIPipelineState()));
    if (!m_workGraph) {
        spdlog::error(
            "HierarchialCullingPass::HierarchialCullingPass CreatePipelines returned null work graph this={} workGraphMode={} rasterOutputKind={}",
            static_cast<const void*>(this),
            static_cast<int>(m_workGraphMode),
            static_cast<int>(m_rasterOutputKind));
    }
    spdlog::info("HierarchialCullingPass::HierarchialCullingPass querying required scratch memory this={} hasWorkGraph={}",
        static_cast<const void*>(this),
        static_cast<bool>(m_workGraph));
    auto memSize = m_workGraph->GetRequiredScratchMemorySize();
    spdlog::info("HierarchialCullingPass::HierarchialCullingPass required scratch memory size={} this={}", memSize, static_cast<const void*>(this));
    m_scratchBuffer = Buffer::CreateShared(
        rhi::HeapType::DeviceLocal,
        memSize,
        true);
    m_scratchBuffer->SetMemoryUsageHint("Work graph scratch buffer");
    m_visibleClustersBuffer = std::move(visibleClustersBuffer);
    m_visibleClustersCounterBuffer = std::move(visibleClustersCounterBuffer);
    m_swVisibleClustersCounterBuffer = std::move(swVisibleClustersCounterBuffer);
    m_pageJobVisibleClustersBuffer = std::move(pageJobVisibleClustersBuffer);
    m_pageJobVisibleClustersCounterBuffer = std::move(pageJobVisibleClustersCounterBuffer);
    m_workGraphComputePageJobDescriptorsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1,
        sizeof(CLodWorkGraphComputePageJobDescriptors),
        false,
        false,
        false,
        false);
    m_workGraphComputePageJobDescriptorsBuffer->SetName("CLod Work Graph Compute Page Job Descriptors");
    m_histogramIndirectCommand = std::move(histogramIndirectCommand);
    m_workGraphTelemetryBuffer = std::move(workGraphTelemetryBuffer);
    m_occlusionReplayBuffer = std::move(occlusionReplayBuffer);
    m_occlusionReplayStateBuffer = std::move(occlusionReplayStateBuffer);
    m_occlusionNodeGpuInputsBuffer = std::move(occlusionNodeGpuInputsBuffer);
    m_viewDepthSrvIndicesBuffer = std::move(viewDepthSrvIndicesBuffer);
    m_viewRasterInfoBuffer = std::move(viewRasterInfoBuffer);
    m_shadowPredictiveInvalidationCandidatesBuffer = std::move(shadowPredictiveInvalidationCandidatesBuffer);
    m_shadowPredictiveInvalidationCandidateCountBuffer = std::move(shadowPredictiveInvalidationCandidateCountBuffer);
    m_shadowInvalidatedInstancesBitsetBuffer = std::move(shadowInvalidatedInstancesBitsetBuffer);
    m_shadowDirtyHierarchyTexture = std::move(shadowDirtyHierarchyTexture);
    m_shadowPageTableTexture = std::move(shadowPageTableTexture);
    m_shadowPhysicalPagesTexture = std::move(shadowPhysicalPagesTexture);
    m_slabResourceGroup = std::move(slabResourceGroup);
    m_phase1VisibleClustersCounterBuffer = std::move(phase1VisibleClustersCounterBuffer);
    m_swWriteBaseCounterBuffer = std::move(swWriteBaseCounterBuffer);
    m_maxVisibleClusters = inputs.maxVisibleClusters;
    m_renderPhase = std::move(inputs.renderPhase);
    m_clodOnlyWorkloads = inputs.clodOnlyWorkloads;
    m_useShadowCascadeViews = inputs.useShadowCascadeViews;
}

HierarchialCullingPass::~HierarchialCullingPass() = default;

void HierarchialCullingPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    auto queryBuilder = ecsWorld.query_builder<>()
        .with<Components::IsActiveDrawSetIndices>()
        .with<Components::ParticipatesInPass>(RendererECSManager::GetInstance().GetRenderPhaseEntity(m_renderPhase));
    if (m_clodOnlyWorkloads) {
        queryBuilder.with<Components::CLodOnlyDrawWorkload>();
    }
    else {
        queryBuilder.with<Components::GeneralDrawWorkload>();
    }
    flecs::query<> drawSetIndicesQuery = queryBuilder.build();
    builder->WithUnorderedAccess(
            m_scratchBuffer,
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_histogramIndirectCommand,
            m_workGraphTelemetryBuffer,
            m_occlusionReplayBuffer,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer)
        .WithUnorderedAccess(
            Builtin::CLod::StreamingLoadRequests,
            Builtin::CLod::StreamingLoadCounter,
            Builtin::CLod::StreamingRuntimeState,
            Builtin::CLod::StreamingTouchedGroupsCounter,
            Builtin::CLod::StreamingTouchedGroups)
        .WithShaderResource(
            Builtin::IndirectCommandBuffers::Master,
            Builtin::CLod::Offsets,
            Builtin::CLod::GroupChunks,
            Builtin::CLod::Groups,
            Builtin::CLod::Segments,
            Builtin::CLod::Nodes,
            Builtin::CLod::MeshletBounds,
            Builtin::CLod::StreamingActiveGroupsBits,
            Builtin::CLod::StreamingNonResidentBits,
            Builtin::CLod::MeshMetadata,
            Builtin::CLod::GroupPageMap,
            Builtin::CullingCameraBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerObjectBuffer,
            Builtin::CameraBuffer,
            Builtin::PerMeshBuffer,
            Builtin::SkeletonResources::InverseBindMatrices,
            Builtin::SkeletonResources::BoneTransforms,
            Builtin::SkeletonResources::SkinningInstanceInfo,
            m_visibleClustersCounterBuffer,
            m_occlusionReplayStateBuffer,
            Builtin::PerMaterialDataBuffer,
            m_workGraphComputePageJobDescriptorResourceId.c_str())
        .WithShaderResource(ECSResourceResolver(drawSetIndicesQuery));

    if (UsesPerViewDepthMapOcclusion(m_rasterOutputKind)) {
        builder->WithUnorderedAccess(m_viewDepthSrvIndicesBuffer)
            .WithShaderResource(Builtin::PrimaryCamera::LinearDepthMap);
    }

    if (UsesSWClassification(m_workGraphMode)) {
        builder->WithUnorderedAccess(m_swVisibleClustersCounterBuffer);
    }
    if (m_pageJobVisibleClustersBuffer && m_pageJobVisibleClustersCounterBuffer) {
        builder->WithUnorderedAccess(m_pageJobVisibleClustersBuffer, m_pageJobVisibleClustersCounterBuffer);
    }
    if (UsesSWClassification(m_workGraphMode)) {
        builder->WithShaderResource(m_viewRasterInfoBuffer);
    }
    if (UsesVirtualShadowOutput(m_rasterOutputKind)) {
        if (m_shadowPredictiveInvalidationCandidatesBuffer && m_shadowPredictiveInvalidationCandidateCountBuffer) {
            builder->WithUnorderedAccess(
                m_shadowPredictiveInvalidationCandidatesBuffer,
                m_shadowPredictiveInvalidationCandidateCountBuffer);
        }
        builder->WithShaderResource(
            Builtin::Shadows::CLodClipmapInfo,
            Builtin::Shadows::CLodCompactShadowCameras,
            m_shadowDirtyHierarchyTexture)
            .WithUnorderedAccess(Builtin::Shadows::CLodPageTable);
        if (m_shadowInvalidatedInstancesBitsetBuffer) {
            builder->WithShaderResource(m_shadowInvalidatedInstancesBitsetBuffer);
        }
    }
    if (UsesWorkGraphSWRaster(m_workGraphMode) && UsesVirtualShadowOutput(m_rasterOutputKind)) {
        builder->WithUnorderedAccess(Builtin::Shadows::CLodPhysicalPages);
    }

    // Phase 2 reads Phase 1's HW counter to offset writes in the visible clusters buffer.
    if (m_phase1VisibleClustersCounterBuffer) {
        builder->WithShaderResource(m_phase1VisibleClustersCounterBuffer);
    }
    if (UsesSWClassification(m_workGraphMode) && m_swWriteBaseCounterBuffer) {
        builder->WithShaderResource(m_swWriteBaseCounterBuffer);
    }

    // Declare visibility buffer UAVs for SW raster render graph tracking.
    if (UsesWorkGraphSWRaster(m_workGraphMode) && UsesVisibilityBufferOutput(m_rasterOutputKind)) {
        for (auto& vb : m_visibilityBuffers) {
            builder->WithUnorderedAccess(vb);
        }
    }
    builder->WithUnorderedAccess(Builtin::DebugVisualization);

    // Declare page pool slabs for bindless access (auto-invalidates when new slabs are added).
    if (m_slabResourceGroup) {
        builder->WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
    }

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void HierarchialCullingPass::Setup() {
    if (UsesWorkGraphSWRaster(m_workGraphMode) && UsesVirtualShadowOutput(m_rasterOutputKind)) {
        RegisterSRV(SRVViewType::Texture2DArrayFull, Builtin::Shadows::CLodPageTable);
    }
}

PassReturn HierarchialCullingPass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

    if (m_pageJobVisibleClustersCounterBuffer) {
        BindResourceDescriptorIndices(commandList, m_clearPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_clearPipelineState.GetAPIPipelineState().GetHandle());

        uint32_t clearRootConstants[NumMiscUintRootConstants] = {};
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] =
            m_pageJobVisibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_VALUE] = 0u;
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] = 1u;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            clearRootConstants);
        commandList.Dispatch(1u, 1u, 1u);

        rhi::BufferBarrier pageJobCounterBarrier{};
        pageJobCounterBarrier.buffer = m_pageJobVisibleClustersCounterBuffer->GetAPIResource().GetHandle();
        pageJobCounterBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        pageJobCounterBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        pageJobCounterBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
        pageJobCounterBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;

        rhi::BarrierBatch clearBarrierBatch{};
        clearBarrierBatch.buffers = { &pageJobCounterBarrier };
        commandList.Barriers(clearBarrierBatch);
    }

    commandList.SetWorkGraph(m_workGraph->GetHandle(), m_scratchBuffer->GetAPIResource().GetHandle(), true);

    BindResourceDescriptorIndices(commandList, m_pipelineResources);

    uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
    uintRootConstants[CLOD_WG_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_WG_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_WG_SW_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_swVisibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_WG_HW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX] = m_histogramIndirectCommand->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_WG_TELEMETRY_DESCRIPTOR_INDEX] = m_workGraphTelemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    if (UsesSWClassification(m_workGraphMode)) {
        uintRootConstants[CLOD_WG_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
    }
    uint32_t workGraphFlags = 0u;
    if (IsCLodWorkGraphTelemetryEnabled()) {
        workGraphFlags |= CLOD_WG_FLAG_TELEMETRY_ENABLED;
    }
    if (UsesPerViewDepthMapOcclusion(m_rasterOutputKind) &&
        SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")()) {
        workGraphFlags |= CLOD_WG_FLAG_OCCLUSION_ENABLED;
    }
    const bool enableSoftwareRaster = UsesSWClassification(m_workGraphMode);
    if (enableSoftwareRaster) {
        workGraphFlags |= CLOD_WG_FLAG_SW_RASTER_ENABLED;
    }
    if (m_workGraphMode == HierarchialCullingWorkGraphMode::SoftwareRasterCompute) {
        workGraphFlags |= CLOD_WG_FLAG_COMPUTE_SW_RASTER;
    }
    if (kDisableVirtualShadowDirtyPageCulling && UsesVirtualShadowOutput(m_rasterOutputKind)) {
        workGraphFlags |= CLOD_WG_FLAG_DISABLE_SHADOW_DIRTY_PAGE_CULLING;
    }
    constexpr uint32_t swRasterThreshold = 16; // pixel diameter threshold
    workGraphFlags |= (swRasterThreshold << CLOD_WG_SW_RASTER_THRESHOLD_SHIFT);
    if (!m_isFirstPass) {
        workGraphFlags |= CLOD_WG_FLAG_PHASE2;
    }
    uintRootConstants[CLOD_WG_FLAGS] = workGraphFlags;

    // Pack page-job VSM flags
    uint32_t pageJobFlags = 0;
    {
        auto& settings = SettingsManager::GetInstance();
        const bool pageJobEnabled =
            CLodVSMRasterModeUsesLargeClusterShadowRouting(
                settings.getSettingGetter<CLodVSMRasterMode>(CLodVSMRasterModeSettingName)());
        if (pageJobEnabled && UsesVirtualShadowOutput(m_rasterOutputKind)) {
            pageJobFlags |= CLOD_WG_PAGE_JOB_FLAG_ENABLED;
        }
        const bool pageJobForceAll = settings.getSettingGetter<bool>(CLodPageJobForceAllSettingName)();
        if (pageJobForceAll) {
            pageJobFlags |= CLOD_WG_PAGE_JOB_FLAG_FORCE_ALL;
        }
        const uint32_t diameterThreshold = std::min(settings.getSettingGetter<uint32_t>(CLodPageJobDiameterThresholdSettingName)(), 255u);
        pageJobFlags |= (diameterThreshold << CLOD_WG_PAGE_JOB_DIAMETER_THRESHOLD_SHIFT);
        const float sparseRatio = settings.getSettingGetter<float>(CLodPageJobSparseRatioSettingName)();
        const uint32_t sparseRatioEncoded = std::min(static_cast<uint32_t>(sparseRatio * 255.0f + 0.5f), 255u);
        pageJobFlags |= (sparseRatioEncoded << CLOD_WG_PAGE_JOB_SPARSE_RATIO_SHIFT);
        const uint32_t maxPages = std::min(settings.getSettingGetter<uint32_t>(CLodPageJobMaxPagesPerClusterSettingName)(), 255u);
        pageJobFlags |= (maxPages << CLOD_WG_PAGE_JOB_MAX_PAGES_SHIFT);
    }
    uintRootConstants[CLOD_WG_PAGE_JOB_FLAGS] = pageJobFlags;
    if (m_shadowPageTableTexture) {
        uintRootConstants[CLOD_WG_VIRTUAL_SHADOW_PAGE_TABLE_UAV_DESCRIPTOR_INDEX] =
            m_shadowPageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    }
    if (m_shadowPhysicalPagesTexture) {
        uintRootConstants[CLOD_WG_VIRTUAL_SHADOW_PHYSICAL_PAGES_UAV_DESCRIPTOR_INDEX] =
            m_shadowPhysicalPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
    }
    uintRootConstants[CLOD_WG_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX] = m_occlusionReplayBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_WG_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX] = m_occlusionReplayStateBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_WG_WORKGRAPH_NODE_INPUTS_DESCRIPTOR_INDEX] = m_occlusionNodeGpuInputsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_WG_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX] =
        UsesPerViewDepthMapOcclusion(m_rasterOutputKind)
            ? m_viewDepthSrvIndicesBuffer->GetSRVInfo(0).slot.index
            : 0u;
    uintRootConstants[CLOD_WG_VISIBLE_CLUSTERS_CAPACITY] = static_cast<uint32_t>(m_maxVisibleClusters);
    uintRootConstants[CLOD_WG_SHADOW_DIRTY_HIERARCHY_DESCRIPTOR_INDEX] =
        m_shadowDirtyHierarchyTexture
            ? m_shadowDirtyHierarchyTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index
            : 0u;
    uintRootConstants[CLOD_WG_SHADOW_INVALIDATED_INSTANCES_DESCRIPTOR_INDEX] =
        m_shadowInvalidatedInstancesBitsetBuffer
            ? m_shadowInvalidatedInstancesBitsetBuffer->GetSRVInfo(0).slot.index
            : 0u;
    uintRootConstants[CLOD_WG_SHADOW_PREDICTIVE_INVALIDATION_CANDIDATES_DESCRIPTOR_INDEX] =
        m_shadowPredictiveInvalidationCandidatesBuffer
            ? m_shadowPredictiveInvalidationCandidatesBuffer->GetUAVShaderVisibleInfo(0).slot.index
            : 0u;
    uintRootConstants[CLOD_WG_SHADOW_PREDICTIVE_INVALIDATION_CANDIDATE_COUNT_DESCRIPTOR_INDEX] =
        m_shadowPredictiveInvalidationCandidateCountBuffer
            ? m_shadowPredictiveInvalidationCandidateCountBuffer->GetUAVShaderVisibleInfo(0).slot.index
            : 0u;

    // Always bind valid SRV descriptors for the aliased write-base slots.
    // Phase 1 does not read these counters, but the shader still forms StructuredBuffer
    // views from the aliased root constants before branching on the phase flag.
    uintRootConstants[CLOD_WG_HW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX] =
        (m_phase1VisibleClustersCounterBuffer ? m_phase1VisibleClustersCounterBuffer : m_visibleClustersCounterBuffer)
            ->GetSRVInfo(0)
            .slot.index;
    uintRootConstants[CLOD_WG_SW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX] =
        (m_swWriteBaseCounterBuffer ? m_swWriteBaseCounterBuffer : m_swVisibleClustersCounterBuffer)
            ->GetSRVInfo(0)
            .slot.index;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        uintRootConstants);

    if (m_isFirstPass) {
        std::vector<ObjectCullRecord> cullRecords;

        ViewFilter filter = GetCullViewFilter(m_useShadowCascadeViews);
        context.viewManager->ForEachFiltered(filter, [&](uint64_t view) {
            auto viewInfo = context.viewManager->Get(view);
            auto cameraBufferIndex = viewInfo->gpu.cameraBufferIndex;
            auto workloads = context.indirectCommandBufferManager->GetViewIndirectBuffersForRenderPhase(
                view,
                m_renderPhase,
                m_clodOnlyWorkloads);
            for (auto& wl : workloads) {
                auto count = wl.workload.count;
                if (count == 0) {
                    continue;
                }
                ObjectCullRecord record{};
                record.viewDataIndex = cameraBufferIndex;
                record.activeDrawSetIndicesSRVIndex = context.objectManager->GetActiveDrawSetIndices(wl.key)->GetSRVInfo(0).slot.index;
                record.activeDrawCount = count;
                record.dispatchGridX = static_cast<uint>((count + 63) / 64);
                record.dispatchGridY = 1;
                record.dispatchGridZ = 1;
                cullRecords.push_back(record);
            }
        });

        rhi::WorkGraphDispatchDesc dispatchDesc{};
        dispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::NodeCpuInput;
        dispatchDesc.nodeCpuInput.entryPointIndex = 0;
        dispatchDesc.nodeCpuInput.pRecords = cullRecords.data();
        dispatchDesc.nodeCpuInput.numRecords = static_cast<uint32_t>(cullRecords.size());
        dispatchDesc.nodeCpuInput.recordByteStride = sizeof(ObjectCullRecord);
        
        // Dispatching a zero-record work graph seems to break the driver on some platforms
        // It was reusing old dispatch records from a previous graph dispatch
		if (!cullRecords.empty()) {
            commandList.DispatchWorkGraph(dispatchDesc);
        }

        rhi::BufferBarrier postWorkGraphBarriers[3] = {};
        postWorkGraphBarriers[0].buffer = m_visibleClustersCounterBuffer->GetAPIResource().GetHandle();
        postWorkGraphBarriers[0].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        postWorkGraphBarriers[0].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        postWorkGraphBarriers[0].beforeSync = rhi::ResourceSyncState::ComputeShading;
        postWorkGraphBarriers[0].afterSync = rhi::ResourceSyncState::ComputeShading;
        postWorkGraphBarriers[1].buffer = m_occlusionReplayStateBuffer->GetAPIResource().GetHandle();
        postWorkGraphBarriers[1].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        postWorkGraphBarriers[1].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        postWorkGraphBarriers[1].beforeSync = rhi::ResourceSyncState::ComputeShading;
        postWorkGraphBarriers[1].afterSync = rhi::ResourceSyncState::ComputeShading;
        postWorkGraphBarriers[2].buffer = m_occlusionReplayBuffer->GetAPIResource().GetHandle();
        postWorkGraphBarriers[2].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        postWorkGraphBarriers[2].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        postWorkGraphBarriers[2].beforeSync = rhi::ResourceSyncState::ComputeShading;
        postWorkGraphBarriers[2].afterSync = rhi::ResourceSyncState::ComputeShading;
        rhi::BarrierBatch bufferBarriers{};
        bufferBarriers.buffers = rhi::Span<rhi::BufferBarrier>(postWorkGraphBarriers, 3);
        commandList.Barriers(bufferBarriers);
    }
    else {
        rhi::BufferBarrier replayDispatchBarriers[2] = {};
        replayDispatchBarriers[0].buffer = m_occlusionReplayBuffer->GetAPIResource().GetHandle();
        replayDispatchBarriers[0].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        replayDispatchBarriers[0].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        replayDispatchBarriers[0].beforeSync = rhi::ResourceSyncState::ComputeShading;
        replayDispatchBarriers[0].afterSync = rhi::ResourceSyncState::ComputeShading;

        replayDispatchBarriers[1].buffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
        replayDispatchBarriers[1].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        replayDispatchBarriers[1].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        replayDispatchBarriers[1].beforeSync = rhi::ResourceSyncState::ComputeShading;
        replayDispatchBarriers[1].afterSync = rhi::ResourceSyncState::ComputeShading;

        rhi::BarrierBatch replayBarrierBatch{};
        replayBarrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(replayDispatchBarriers, 2);
        commandList.Barriers(replayBarrierBatch);

        rhi::WorkGraphDispatchDesc replayDispatchDesc{};
        replayDispatchDesc.dispatchMode = rhi::WorkGraphDispatchMode::MultiNodeGpuInput;
        replayDispatchDesc.multiNodeGpuInput.inputBuffer = m_occlusionNodeGpuInputsBuffer->GetAPIResource().GetHandle();
        replayDispatchDesc.multiNodeGpuInput.inputAddressOffset = 0;
        commandList.DispatchWorkGraph(replayDispatchDesc);
    }

    std::array<rhi::BufferBarrier, 2> counterBarriers{};
    counterBarriers[0].buffer = m_visibleClustersCounterBuffer->GetAPIResource().GetHandle();
    counterBarriers[0].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    counterBarriers[0].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    counterBarriers[0].beforeSync = rhi::ResourceSyncState::ComputeShading;
    counterBarriers[0].afterSync = rhi::ResourceSyncState::ComputeShading;
    uint32_t counterBarrierCount = 1;
    if (UsesSWClassification(m_workGraphMode)) {
        counterBarriers[1].buffer = m_swVisibleClustersCounterBuffer->GetAPIResource().GetHandle();
        counterBarriers[1].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        counterBarriers[1].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        counterBarriers[1].beforeSync = rhi::ResourceSyncState::ComputeShading;
        counterBarriers[1].afterSync = rhi::ResourceSyncState::ComputeShading;
        counterBarrierCount = 2;
    }
    rhi::BarrierBatch counterBarrierBatch{};
    counterBarrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(counterBarriers.data(), counterBarrierCount);
    commandList.Barriers(counterBarrierBatch);

    BindResourceDescriptorIndices(commandList, m_createCommandPipelineState.GetResourceDescriptorSlots());
    // Reset aliased slots for CreateRasterBucketsHistogramCommandCSMain.
    // The work-graph dispatch binds slot 3/6 as UAV-facing descriptors, but the create-command
    // shader reads them as SRVs when building the indirect dispatch arguments.
    uintRootConstants[CLOD_CREATE_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_CREATE_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX] = m_histogramIndirectCommand->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_CREATE_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX] = m_occlusionReplayStateBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_CREATE_WORKGRAPH_NODE_INPUTS_DESCRIPTOR_INDEX] = m_occlusionNodeGpuInputsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_CREATE_NUM_RASTER_BUCKETS] = context.materialManager->GetRasterBucketCount();
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        uintRootConstants);

    commandList.BindPipeline(m_createCommandPipelineState.GetAPIPipelineState().GetHandle());
    commandList.Dispatch(1, 1, 1);

    return {};
}

void HierarchialCullingPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
    if (!updateContext) {
        return;
    }
    auto& context = *updateContext;
    m_declaredResourcesChanged = false;

    uint32_t zero = 0u;
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_visibleClustersCounterBuffer), 0);
    if (UsesSWClassification(m_workGraphMode)) {
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_swVisibleClustersCounterBuffer), 0);
    }

    CLodWorkGraphComputePageJobDescriptors pageJobDescriptors{};
    if (m_pageJobVisibleClustersBuffer && m_pageJobVisibleClustersCounterBuffer) {
        pageJobDescriptors.visibleClustersUAVDescriptorIndex = m_pageJobVisibleClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        pageJobDescriptors.visibleClustersCounterUAVDescriptorIndex = m_pageJobVisibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    }
    BUFFER_UPLOAD(
        &pageJobDescriptors,
        sizeof(CLodWorkGraphComputePageJobDescriptors),
        rg::runtime::UploadTarget::FromShared(m_workGraphComputePageJobDescriptorsBuffer),
        0);

    // Keep the shared per-view visibility UAV table valid for any visibility-buffer path.
    // Reyes patch raster consumes this buffer even when the primary CLod path is not using SW classification.
    if (UsesVisibilityBufferOutput(m_rasterOutputKind) || UsesSWClassification(m_workGraphMode)) {
        m_visibilityBuffers.clear();
        auto numViews = context.viewManager->GetCameraBufferSize();
        std::vector<CLodViewRasterInfo> viewRasterInfo(numViews);
        const CLodVirtualShadowResolutionConfig virtualShadowConfig = CLodVirtualShadowBuildRuntimeResolutionConfig();
        context.viewManager->ForEachView([&](uint64_t v) {
            auto viewInfo = context.viewManager->Get(v);
            if (!viewInfo) {
                return;
            }

            auto cameraIndex = viewInfo->gpu.cameraBufferIndex;
            CLodViewRasterInfo info{};
            info.scissorMinX = 0;
            info.scissorMinY = 0;

            if (UsesVirtualShadowOutput(m_rasterOutputKind)) {
                if (viewInfo->flags.shadow && viewInfo->lightType == Components::LightType::Directional) {
                    info.scissorMaxX = virtualShadowConfig.virtualResolution;
                    info.scissorMaxY = virtualShadowConfig.virtualResolution;
                    info.viewportScaleX = 1.0f;
                    info.viewportScaleY = 1.0f;
                }
                viewRasterInfo[cameraIndex] = info;
                return;
            }

            if (viewInfo->gpu.visibilityBuffer != nullptr) {
                info.visibilityUAVDescriptorIndex = viewInfo->gpu.visibilityBuffer->GetUAVShaderVisibleInfo(0).slot.index;
                info.scissorMaxX = viewInfo->gpu.visibilityBuffer->GetWidth();
                info.scissorMaxY = viewInfo->gpu.visibilityBuffer->GetHeight();
                info.viewportScaleX = 1.0f;
                info.viewportScaleY = 1.0f;
                viewRasterInfo[cameraIndex] = info;
                if (UsesWorkGraphSWRaster(m_workGraphMode)) {
                    m_visibilityBuffers.push_back(viewInfo->gpu.visibilityBuffer);
                }
            }
        });

        m_viewRasterInfoBuffer->ResizeStructured(static_cast<uint32_t>(viewRasterInfo.size()));
        BUFFER_UPLOAD(
            viewRasterInfo.data(),
            static_cast<uint32_t>(viewRasterInfo.size() * sizeof(CLodViewRasterInfo)),
            rg::runtime::UploadTarget::FromShared(m_viewRasterInfoBuffer),
            0);
        if (UsesWorkGraphSWRaster(m_workGraphMode)) {
            m_declaredResourcesChanged = true;
        }
    }
    else {
        m_visibilityBuffers.clear();
        m_declaredResourcesChanged = false;
    }

    if (!m_isFirstPass) {
        return;
    }

    CLodReplayBufferState replayState{};
    replayState.nodeWriteCount = 0;
    replayState.meshletWriteCount = 0;
    replayState.nodeDropped = 0;
    replayState.meshletDropped = 0;
    replayState.visibleClusterCombinedCount = 0;
    BUFFER_UPLOAD(
        &replayState,
        sizeof(CLodReplayBufferState),
        rg::runtime::UploadTarget::FromShared(m_occlusionReplayStateBuffer),
        0);

    if (UsesPerViewDepthMapOcclusion(m_rasterOutputKind)) {
        std::vector<CLodViewDepthSRVIndex> viewDepthSrvIndices(CLodMaxViewDepthIndices);
        const bool useHistoryDepth = m_isFirstPass;
        for (uint32_t i = 0; i < CLodMaxViewDepthIndices; ++i) {
            viewDepthSrvIndices[i].cameraBufferIndex = i;
            viewDepthSrvIndices[i].linearDepthSRVIndex = 0;
        }

        context.viewManager->ForEachView([&](uint64_t viewID) {
            const auto* view = context.viewManager->Get(viewID);
            if (!view) {
                return;
            }

            const uint32_t cameraBufferIndex = view->gpu.cameraBufferIndex;
            if (cameraBufferIndex >= CLodMaxViewDepthIndices) {
                return;
            }

            const auto linearDepthMap = useHistoryDepth
                ? (view->gpu.lastFrameLinearDepthValid ? view->gpu.lastFrameLinearDepthMap : nullptr)
                : view->gpu.linearDepthMap;
            if (!linearDepthMap) {
                return;
            }

            uint32_t slice = 0;
            if (view->cameraInfo.depthBufferArrayIndex >= 0) {
                slice = static_cast<uint32_t>(view->cameraInfo.depthBufferArrayIndex);
            }

            const uint32_t maxSlices = linearDepthMap->GetNumSRVSlices();
            if (maxSlices == 0) {
                return;
            }

            slice = (std::min)(slice, maxSlices - 1);
            viewDepthSrvIndices[cameraBufferIndex].cameraBufferIndex = cameraBufferIndex;
            viewDepthSrvIndices[cameraBufferIndex].linearDepthSRVIndex = linearDepthMap->GetSRVInfo(0, slice).slot.index;
        });

        BUFFER_UPLOAD(
            viewDepthSrvIndices.data(),
            static_cast<uint32_t>(viewDepthSrvIndices.size() * sizeof(CLodViewDepthSRVIndex)),
            rg::runtime::UploadTarget::FromShared(m_viewDepthSrvIndicesBuffer),
            0);
    }

    CLodNodeGpuInput nodeGpuInputs[3] = {};
    CLodMultiNodeGpuInput multiNodeGpuInput{};
    multiNodeGpuInput.numNodeInputs = 2;
    multiNodeGpuInput.pad0 = 0;
    multiNodeGpuInput.nodeInputStride = sizeof(CLodNodeGpuInput);

    // Replay dispatch descriptors need stable GPU virtual addresses during Update.
    // These CLod-owned control buffers are small non-aliased resources, so it is safe to materialize
    // them eagerly here before building the replay node input table.
    if (!m_occlusionNodeGpuInputsBuffer->IsMaterialized()) {
        m_occlusionNodeGpuInputsBuffer->Materialize();
    }
    if (!m_occlusionReplayBuffer->IsMaterialized()) {
        m_occlusionReplayBuffer->Materialize();
    }

    if (ID3D12Resource* nodeInputResource = rhi::dx12::get_resource(m_occlusionNodeGpuInputsBuffer->GetAPIResource())) {
        const uint64_t nodeInputBufferAddress = nodeInputResource->GetGPUVirtualAddress();
        multiNodeGpuInput.nodeInputsAddress = nodeInputBufferAddress + sizeof(CLodNodeGpuInput);
    }

    if (ID3D12Resource* replayResource = rhi::dx12::get_resource(m_occlusionReplayBuffer->GetAPIResource())) {
        const uint64_t replayAddress = replayResource->GetGPUVirtualAddress();

        // Entry point 1 = TraverseNodes — node replay region at offset 0
        nodeGpuInputs[1].entrypointIndex = 1;
        nodeGpuInputs[1].numRecords = 0; // patched by GPU in CreateRasterBucketsHistogramCommandCSMain
        nodeGpuInputs[1].recordsAddress = replayAddress;
        nodeGpuInputs[1].recordStride = CLodNodeReplayStrideBytes;

        // Entry point 2 = ClusterCull1 — meshlet replay region at midpoint offset
        nodeGpuInputs[2].entrypointIndex = 2;
        nodeGpuInputs[2].numRecords = 0; // patched by GPU in CreateRasterBucketsHistogramCommandCSMain
        nodeGpuInputs[2].recordsAddress = replayAddress + CLodReplayMeshletRegionOffset;
        nodeGpuInputs[2].recordStride = CLodMeshletReplayStrideBytes;
    }

    static_assert(sizeof(CLodMultiNodeGpuInput) == sizeof(CLodNodeGpuInput));
    std::memcpy(&nodeGpuInputs[0], &multiNodeGpuInput, sizeof(CLodMultiNodeGpuInput));

    BUFFER_UPLOAD(
        nodeGpuInputs,
        sizeof(nodeGpuInputs),
        rg::runtime::UploadTarget::FromShared(m_occlusionNodeGpuInputsBuffer),
        0);

    if (IsCLodWorkGraphTelemetryEnabled()) {
        std::vector<uint32_t> zeroTelemetry(CLodWorkGraphCounterCount, 0u);
        BUFFER_UPLOAD(
            zeroTelemetry.data(),
            static_cast<uint32_t>(zeroTelemetry.size() * sizeof(uint32_t)),
            rg::runtime::UploadTarget::FromShared(m_workGraphTelemetryBuffer),
            0);
    }
}

bool HierarchialCullingPass::DeclaredResourcesChanged() const {
    return m_declaredResourcesChanged;
}

std::shared_ptr<Resource> HierarchialCullingPass::ProvideResource(ResourceIdentifier const& key)
{
    if (key == m_workGraphComputePageJobDescriptorResourceId) {
        return m_workGraphComputePageJobDescriptorsBuffer;
    }

    return nullptr;
}

std::vector<ResourceIdentifier> HierarchialCullingPass::GetSupportedKeys()
{
    return { ResourceIdentifier{ m_workGraphComputePageJobDescriptorResourceId } };
}

void HierarchialCullingPass::Cleanup() {
}

void HierarchialCullingPass::CreatePipelines(
    rhi::Device device,
    rhi::PipelineLayoutHandle globalRootSignature,
    rhi::WorkGraphPtr& outGraph,
    PipelineState& outCreateCommandPipeline,
    PipelineState& outClearPipeline)
{
    spdlog::info(
        "HierarchialCullingPass::CreatePipelines begin this={} deviceValid={} workGraphMode={} rasterOutputKind={} globalRootSignatureValid={}",
        static_cast<const void*>(this),
        device.IsValid(),
        static_cast<int>(m_workGraphMode),
        static_cast<int>(m_rasterOutputKind),
        globalRootSignature.valid());
    WorkGraphFeatureInfo workGraphFeatureInfo{};
    const rhi::Result workGraphFeatureResult = device.QueryFeatureInfo(&workGraphFeatureInfo.header);
    spdlog::info(
        "HierarchialCullingPass::CreatePipelines work graph feature query result={} computeNodes={} meshNodes={} level={}",
        static_cast<int>(workGraphFeatureResult),
        workGraphFeatureInfo.computeNodes,
        workGraphFeatureInfo.meshNodes,
        static_cast<int>(workGraphFeatureInfo.Level()));
    ShaderLibraryInfo libInfo(L"shaders/ClusterLOD/workGraphCulling.hlsl", L"lib_6_8");
    std::wstring pageJobDescriptorResourceIdWide(
        m_workGraphComputePageJobDescriptorResourceId.begin(),
        m_workGraphComputePageJobDescriptorResourceId.end());
    std::wstring pageJobDescriptorResourceIdDefine = L"\"" + pageJobDescriptorResourceIdWide + L"\"";
    std::vector<DxcDefine> defines = {
        { L"CLOD_WG_ENABLE_SW_CLASSIFICATION", UsesSWClassification(m_workGraphMode) ? L"1" : L"0" },
        { L"CLOD_WG_ENABLE_SW_NODE_OUTPUT", UsesWorkGraphSWRaster(m_workGraphMode) ? L"1" : L"0" },
        { L"CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW", UsesVirtualShadowOutput(m_rasterOutputKind) ? L"1" : L"0" },
        { L"CLOD_WG_COMPUTE_PAGE_JOB_DESCRIPTOR_BUFFER_ID", pageJobDescriptorResourceIdDefine.c_str() },
    };
    auto compiled = PSOManager::GetInstance().CompileShaderLibrary(libInfo, defines);
    m_pipelineResources = compiled.resourceDescriptorSlots;

    rhi::ShaderBinary libDxil{
        compiled.libraryBlob->GetBufferPointer(),
        static_cast<uint32_t>(compiled.libraryBlob->GetBufferSize())
    };

    std::vector<rhi::ShaderExportDesc> exports = {
        { "WG_ObjectCull", nullptr },
        { "WG_TraverseNodes", nullptr },
        { "WG_ClusterCull1", nullptr },
        { "WG_ClusterCull2", nullptr },
        { "WG_ClusterCull4", nullptr },
        { "WG_ClusterCull8", nullptr },
        { "WG_ClusterCull16", nullptr },
        { "WG_ClusterCull32", nullptr },
        { "WG_ClusterCull64", nullptr },
    };
    if (UsesWorkGraphSWRaster(m_workGraphMode)) {
        exports.push_back({ "WG_SWRaster", nullptr });
        if (UsesVirtualShadowOutput(m_rasterOutputKind)) {
            exports.push_back({ "WG_PageJobBuild", nullptr });
            exports.push_back({ "WG_PageJobExpand", nullptr });
            exports.push_back({ "WG_PageJobRasterPage", nullptr });
        }
    }

    rhi::ShaderLibraryDesc library{};
    library.dxil = libDxil;
    library.exports = rhi::Span<rhi::ShaderExportDesc>(exports.data(), static_cast<uint32_t>(exports.size()));

    std::array<rhi::NodeIDDesc, 3> entrypoints = {{
        { "ObjectCull", 0 },
        { "TraverseNodes", 0 },
        { "ClusterCull1", 0 }
    }};

    rhi::WorkGraphDesc wg{};
    wg.programName = "HierarchialCulling";
    wg.flags = rhi::WorkGraphFlags::WorkGraphFlagsIncludeAllAvailableNodes;
    wg.globalRootSignature = globalRootSignature;
    wg.libraries = rhi::Span<rhi::ShaderLibraryDesc>(&library, 1);
    wg.entrypoints = rhi::Span<rhi::NodeIDDesc>(entrypoints.data(), static_cast<uint32_t>(entrypoints.size()));
    wg.allowStateObjectAdditions = false;
    switch (m_workGraphMode) {
    case HierarchialCullingWorkGraphMode::HardwareOnly:
        wg.debugName = "HierarchialCullingWG.HW";
        break;
    case HierarchialCullingWorkGraphMode::SoftwareRasterCompute:
        wg.debugName = "HierarchialCullingWG.ComputeSW";
        break;
    case HierarchialCullingWorkGraphMode::SoftwareRasterWorkGraph:
        wg.debugName = "HierarchialCullingWG.WorkGraphSW";
        break;
    }

    spdlog::info(
        "HierarchialCullingPass::CreatePipelines before CreateWorkGraph debugName='{}' program='{}' exports={} entrypoints={}",
        wg.debugName ? wg.debugName : "",
        wg.programName ? wg.programName : "",
        exports.size(),
        entrypoints.size());
    device.CreateWorkGraph(wg, outGraph);
    spdlog::info(
        "HierarchialCullingPass::CreatePipelines after CreateWorkGraph outGraph={}",
        static_cast<bool>(outGraph));

    spdlog::info("HierarchialCullingPass::CreatePipelines before create command pipeline");
    outCreateCommandPipeline = PSOManager::GetInstance().MakeComputePipeline(
        globalRootSignature,
        L"shaders/ClusterLOD/clodUtil.hlsl",
        L"CreateRasterBucketsHistogramCommandCSMain",
        {},
        "HierarchialLODCommandCreation");
    spdlog::info("HierarchialCullingPass::CreatePipelines after create command pipeline valid={}", static_cast<bool>(outCreateCommandPipeline.GetAPIPipelineState()));
    spdlog::info("HierarchialCullingPass::CreatePipelines before clear pipeline");
    outClearPipeline = PSOManager::GetInstance().MakeComputePipeline(
        globalRootSignature,
        L"shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUintStructuredBufferCSMain",
        {},
        "HierarchialCullingClearUintPSO");
    spdlog::info("HierarchialCullingPass::CreatePipelines after clear pipeline valid={}", static_cast<bool>(outClearPipeline.GetAPIPipelineState()));
}
