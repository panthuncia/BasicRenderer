#include "Render/GraphExtensions/ClusterLOD/HierarchicalDispatchCullingPass.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "BuiltinResources.h"
#include "Managers/MaterialManager.h"
#include "Managers/MeshManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Managers/ObjectManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Resources/components.h"
#include "Resources/Resolvers/ECSResourceResolver.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "ShaderBuffers.h"
#include <rhi_dx12_casting.h>
#include "../shaders/PerPassRootConstants/clodClearUintBufferRootConstants.h"
#include "../shaders/PerPassRootConstants/clodCreateCommandRootConstants.h"
#include "../shaders/PerPassRootConstants/clodPureComputeCullingRootConstants.h"
#include "../shaders/PerPassRootConstants/clodWorkGraphRootConstants.h"

namespace {

constexpr uint32_t kPureComputeObjectCullThreadsPerGroup = 64u;
constexpr uint32_t kPureComputeTraverseThreadsPerGroup = 64u;
constexpr uint32_t kPureComputeClusterThreadsPerGroup = 32u;
constexpr uint32_t kPureComputeDenseClusterThreadsPerGroup = 64u;
constexpr uint32_t kPureComputeDenseClusterExpansionFactor = 64u;
constexpr uint32_t kPureComputeMaxTraversalLevels = 64u;
constexpr bool kDisableVirtualShadowDirtyPageCulling = false; 

uint32_t GetPureComputeDenseClusterWorkCapacity(uint32_t maxVisibleClusters)
{
	return std::max(1u, maxVisibleClusters);
}

bool UsesVisibilityBufferOutput(CLodRasterOutputKind outputKind)
{
    return outputKind == CLodRasterOutputKind::VisibilityBuffer;
}

bool UsesDeepVisibilityOutput(CLodRasterOutputKind outputKind)
{
    return outputKind == CLodRasterOutputKind::DeepVisibility;
}

bool UsesVirtualShadowOutput(CLodRasterOutputKind outputKind)
{
    return outputKind == CLodRasterOutputKind::VirtualShadow;
}

bool UsesSWClassification(HierarchicalCullingWorkGraphMode mode)
{
    return mode == HierarchicalCullingWorkGraphMode::SoftwareRasterCompute
        || mode == HierarchicalCullingWorkGraphMode::SoftwareRasterWorkGraph;
}

bool UsesPerViewDepthMapOcclusion(CLodRasterOutputKind outputKind)
{
    return !UsesVirtualShadowOutput(outputKind);
}

std::vector<uint64_t> CollectDeclaredDrawSetResourceIds(RenderPhase renderPhase, bool clodOnlyWorkloads)
{
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();
    auto queryBuilder = ecsWorld.query_builder<>()
        .with<Components::IsActiveDrawSetIndices>()
        .with<Components::ParticipatesInPass>(RendererECSManager::GetInstance().GetRenderPhaseEntity(renderPhase));
    if (clodOnlyWorkloads) {
        queryBuilder.with<Components::CLodOnlyDrawWorkload>();
    }
    else {
        queryBuilder.with<Components::GeneralDrawWorkload>();
    }

    std::vector<uint64_t> resourceIds;
    queryBuilder.build().each([&](flecs::entity entity) {
        if (const auto resource = entity.try_get<Components::Resource>(); resource) {
            if (const auto shared = resource->resource.lock(); shared) {
                resourceIds.push_back(shared->GetGlobalResourceID());
            }
        }
    });

    std::sort(resourceIds.begin(), resourceIds.end());
    resourceIds.erase(std::unique(resourceIds.begin(), resourceIds.end()), resourceIds.end());
    return resourceIds;
}

constexpr uint32_t kReplaySourceNodes = 0u;
constexpr uint32_t kReplaySourceClusters = 1u;

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

HierarchicalDispatchCullingPass::HierarchicalDispatchCullingPass(
    std::string stablePassIdentifier,
    HierarchicalCullingPassInputs inputs,
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> visibleClustersCounterBuffer,
    std::shared_ptr<Buffer> swVisibleClustersCounterBuffer,
    std::shared_ptr<Buffer> voxelRasterWorkBuffer,
    std::shared_ptr<Buffer> voxelRasterWorkCounterBuffer,
    uint32_t voxelRasterWorkCapacity,
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
    std::shared_ptr<PixelBuffer> shadowPhysicalPagesTexture)
    : m_visibleClustersBuffer(std::move(visibleClustersBuffer))
    , m_visibleClustersCounterBuffer(std::move(visibleClustersCounterBuffer))
    , m_swVisibleClustersCounterBuffer(std::move(swVisibleClustersCounterBuffer))
    , m_voxelRasterWorkBuffer(std::move(voxelRasterWorkBuffer))
    , m_voxelRasterWorkCounterBuffer(std::move(voxelRasterWorkCounterBuffer))
    , m_voxelRasterWorkCapacity(voxelRasterWorkCapacity)
    , m_pageJobVisibleClustersBuffer(std::move(pageJobVisibleClustersBuffer))
    , m_pageJobVisibleClustersCounterBuffer(std::move(pageJobVisibleClustersCounterBuffer))
    , m_histogramIndirectCommand(std::move(histogramIndirectCommand))
    , m_workGraphTelemetryBuffer(std::move(workGraphTelemetryBuffer))
    , m_occlusionReplayBuffer(std::move(occlusionReplayBuffer))
    , m_occlusionReplayStateBuffer(std::move(occlusionReplayStateBuffer))
    , m_occlusionNodeGpuInputsBuffer(std::move(occlusionNodeGpuInputsBuffer))
    , m_viewDepthSrvIndicesBuffer(std::move(viewDepthSrvIndicesBuffer))
    , m_viewRasterInfoBuffer(std::move(viewRasterInfoBuffer))
    , m_phase1VisibleClustersCounterBuffer(std::move(phase1VisibleClustersCounterBuffer))
    , m_swWriteBaseCounterBuffer(std::move(swWriteBaseCounterBuffer))
    , m_shadowDirtyHierarchyTexture(std::move(shadowDirtyHierarchyTexture))
    , m_shadowPredictiveInvalidationCandidatesBuffer(std::move(shadowPredictiveInvalidationCandidatesBuffer))
    , m_shadowPredictiveInvalidationCandidateCountBuffer(std::move(shadowPredictiveInvalidationCandidateCountBuffer))
    , m_shadowInvalidatedInstancesBitsetBuffer(std::move(shadowInvalidatedInstancesBitsetBuffer))
    , m_shadowPageTableTexture(std::move(shadowPageTableTexture))
    , m_shadowPhysicalPagesTexture(std::move(shadowPhysicalPagesTexture))
    , m_slabResourceGroup(std::move(slabResourceGroup))
{
    m_isFirstPass = inputs.isFirstPass;
    m_maxVisibleClusters = inputs.maxVisibleClusters;
    m_workGraphMode = inputs.workGraphMode;
    m_rasterOutputKind = inputs.rasterOutputKind;
    m_workGraphComputePageJobDescriptorResourceId =
        std::string(CLodWorkGraphComputePageJobDescriptorBufferId) + "." + std::move(stablePassIdentifier);
    m_voxelRasterQueueDescriptorResourceId =
        std::string(CLodVoxelRasterQueueDescriptorBufferId) + "." + m_workGraphComputePageJobDescriptorResourceId;
    m_renderPhase = std::move(inputs.renderPhase);
    m_clodOnlyWorkloads = inputs.clodOnlyWorkloads;
    m_useShadowCascadeViews = inputs.useShadowCascadeViews;

    if (m_pageJobVisibleClustersBuffer && m_pageJobVisibleClustersCounterBuffer) {
        m_workGraphComputePageJobDescriptorsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
            1u,
            sizeof(CLodWorkGraphComputePageJobDescriptors),
            false,
            false,
            false,
            false);
        m_workGraphComputePageJobDescriptorsBuffer->SetName("CLod Pure Compute Page Job Descriptors");
    }

    m_voxelRasterQueueDescriptorsBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        1u,
        sizeof(CLodVoxelRasterQueueDescriptors),
        false,
        false,
        false,
        false);
    m_voxelRasterQueueDescriptorsBuffer->SetName("CLod Pure Compute Voxel Raster Queue Descriptors");

    const uint32_t frontierCapacity = std::max(1u, m_maxVisibleClusters);
    m_pureComputeCurrentNodeFrontierBuffer = CreateAliasedUnmaterializedStructuredBuffer(frontierCapacity, CLodNodeReplayStrideBytes, true, false, false, true);
    m_pureComputeCurrentNodeFrontierBuffer->SetName("CLod Pure Compute Current Node Frontier");
    m_pureComputeNextNodeFrontierBuffer = CreateAliasedUnmaterializedStructuredBuffer(frontierCapacity, CLodNodeReplayStrideBytes, true, false, false, true);
    m_pureComputeNextNodeFrontierBuffer->SetName("CLod Pure Compute Next Node Frontier");
    m_pureComputeClusterFrontierBuffer = CreateAliasedUnmaterializedStructuredBuffer(frontierCapacity, CLodMeshletReplayStrideBytes, true, false, false, true);
    m_pureComputeClusterFrontierBuffer->SetName("CLod Pure Compute Cluster Frontier");
    m_pureComputeCurrentNodeCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1u, sizeof(uint32_t), true, false, false, false);
    m_pureComputeCurrentNodeCounterBuffer->SetName("CLod Pure Compute Current Node Counter");
    m_pureComputeNextNodeCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1u, sizeof(uint32_t), true, false, false, false);
    m_pureComputeNextNodeCounterBuffer->SetName("CLod Pure Compute Next Node Counter");
    m_pureComputeClusterCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1u, sizeof(uint32_t), true, false, false, false);
    m_pureComputeClusterCounterBuffer->SetName("CLod Pure Compute Cluster Counter");
    m_pureComputeDenseClusterWorkBuffer = CreateAliasedUnmaterializedStructuredBuffer(
        GetPureComputeDenseClusterWorkCapacity(frontierCapacity),
        CLodDenseClusterWorkStrideBytes,
        true,
        false,
        false,
        true);
    m_pureComputeDenseClusterWorkBuffer->SetName("CLod Pure Compute Dense Cluster Work");
    m_pureComputeDenseClusterWorkCounterBuffer = CreateAliasedUnmaterializedStructuredBuffer(1u, sizeof(uint32_t), true, false, false, false);
    m_pureComputeDenseClusterWorkCounterBuffer->SetName("CLod Pure Compute Dense Cluster Work Counter");
    m_pureComputeNodeDispatchArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1u, sizeof(PureComputeDispatchCommand), true, false, false, false);
    m_pureComputeNodeDispatchArgsBuffer->SetName("CLod Pure Compute Node Dispatch Args");
    m_pureComputeClusterDispatchArgsBuffer = CreateAliasedUnmaterializedStructuredBuffer(1u, sizeof(PureComputeDispatchCommand), true, false, false, false);
    m_pureComputeClusterDispatchArgsBuffer->SetName("CLod Pure Compute Cluster Dispatch Args");

    const bool enableSharedSWClassificationPath =
        UsesSWClassification(m_workGraphMode) || m_workGraphComputePageJobDescriptorsBuffer != nullptr;
    const bool enableComputePageJobDescriptorBuffer =
        m_workGraphComputePageJobDescriptorsBuffer != nullptr;
    std::wstring pageJobDescriptorResourceIdWide(
        m_workGraphComputePageJobDescriptorResourceId.begin(),
        m_workGraphComputePageJobDescriptorResourceId.end());
    std::wstring pageJobDescriptorResourceIdDefine = L"\"" + pageJobDescriptorResourceIdWide + L"\"";
    std::wstring voxelQueueDescriptorResourceIdWide(
        m_voxelRasterQueueDescriptorResourceId.begin(),
        m_voxelRasterQueueDescriptorResourceId.end());
    std::wstring voxelQueueDescriptorResourceIdDefine = L"\"" + voxelQueueDescriptorResourceIdWide + L"\"";
    std::vector<DxcDefine> pureComputeDefines = {
        { L"CLOD_WG_ENABLE_SW_CLASSIFICATION", enableSharedSWClassificationPath ? L"1" : L"0" },
        { L"CLOD_WG_ENABLE_SW_NODE_OUTPUT", L"0" },
        { L"CLOD_SW_RASTER_OUTPUT_VIRTUAL_SHADOW", UsesVirtualShadowOutput(m_rasterOutputKind) ? L"1" : L"0" },
        { L"CLOD_WG_ENABLE_COMPUTE_PAGE_JOB_DESCRIPTOR_BUFFER", enableComputePageJobDescriptorBuffer ? L"1" : L"0" },
        { L"CLOD_COMPUTE_INCLUDE_ONLY", L"1" },
        { L"CLOD_WG_VOXEL_RASTER_QUEUE_DESCRIPTOR_BUFFER_ID", voxelQueueDescriptorResourceIdDefine.c_str() },
    };
    if (enableComputePageJobDescriptorBuffer) {
        pureComputeDefines.push_back({ L"CLOD_WG_COMPUTE_PAGE_JOB_DESCRIPTOR_BUFFER_ID", pageJobDescriptorResourceIdDefine.c_str() });
    }

    auto& psoManager = PSOManager::GetInstance();
    const auto computeLayout = psoManager.GetComputeRootSignature().GetHandle();
    m_clearPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUintStructuredBufferCSMain",
        {},
        "HierarchicalDispatchCulling.ClearUint");
    m_createCommandPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/clodUtil.hlsl",
        L"CreateRasterBucketsHistogramCommandCSMain",
        {},
        "HierarchicalDispatchCulling.CreateRasterBucketsHistogramCommand");
    m_pureComputeBuildDispatchArgsPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"BuildPureComputeDispatchArgsCS",
        pureComputeDefines,
        "CLod.PureCompute.BuildDispatchArgs");
    m_pureComputeBuildReplayDispatchArgsPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"BuildPureComputeReplayDispatchArgsCS",
        pureComputeDefines,
        "CLod.PureCompute.BuildReplayDispatchArgs");
    m_pureComputeObjectCullPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"PureComputeObjectCullCS",
        pureComputeDefines,
        "CLod.PureCompute.ObjectCull");
    m_pureComputeReplayNodesPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"SeedPureComputeReplayNodesCS",
        pureComputeDefines,
        "CLod.PureCompute.SeedReplayNodes");
    m_pureComputeReplayClustersPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"SeedPureComputeReplayClustersCS",
        pureComputeDefines,
        "CLod.PureCompute.SeedReplayClusters");
    m_pureComputeTraversePipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"PureComputeTraverseFrontierCS",
        pureComputeDefines,
        "CLod.PureCompute.Traverse");
    m_pureComputeExpandClustersPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"PureComputeExpandClusterFrontierCS",
        pureComputeDefines,
        "CLod.PureCompute.ExpandClusterFrontier");
    m_pureComputeClusterPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"PureComputeClusterFrontierCS",
        pureComputeDefines,
        "CLod.PureCompute.ClusterCull");
    m_pureComputeDenseClusterPipelineState = psoManager.MakeComputePipeline(
        computeLayout,
        L"shaders/ClusterLOD/computeCulling.hlsl",
        L"PureComputeDenseClusterWorkCS",
        pureComputeDefines,
        "CLod.PureCompute.DenseClusterCull");

    rhi::IndirectArg dispatchArg[] = {
        {.kind = rhi::IndirectArgKind::Dispatch }
    };
    DeviceManager::GetInstance().GetDevice().CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(dispatchArg, 1), sizeof(PureComputeDispatchCommand) },
        computeLayout,
        m_pureComputeDispatchCommandSignature);
}

HierarchicalDispatchCullingPass::~HierarchicalDispatchCullingPass() = default;

void HierarchicalDispatchCullingPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
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
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_voxelRasterWorkBuffer,
            m_voxelRasterWorkCounterBuffer,
            m_histogramIndirectCommand,
            m_workGraphTelemetryBuffer,
            m_occlusionReplayBuffer,
            m_occlusionReplayStateBuffer,
            m_occlusionNodeGpuInputsBuffer,
            m_pureComputeCurrentNodeFrontierBuffer,
            m_pureComputeNextNodeFrontierBuffer,
            m_pureComputeClusterFrontierBuffer,
            m_pureComputeCurrentNodeCounterBuffer,
            m_pureComputeNextNodeCounterBuffer,
            m_pureComputeClusterCounterBuffer,
            m_pureComputeDenseClusterWorkBuffer,
            m_pureComputeDenseClusterWorkCounterBuffer,
            m_pureComputeNodeDispatchArgsBuffer,
            m_pureComputeClusterDispatchArgsBuffer,
            Builtin::CLod::StreamingLoadRequests,
            Builtin::CLod::StreamingLoadCounter,
            Builtin::CLod::StreamingTouchedGroupsCounter,
            Builtin::CLod::StreamingTouchedGroups)
        .WithShaderResource(
            Builtin::IndirectCommandBuffers::Master,
            Builtin::CLod::Offsets,
            Builtin::CLod::Groups,
            Builtin::CLod::Segments,
            Builtin::CLod::Nodes,
            Builtin::CLod::StreamingNonResidentBits,
            Builtin::CLod::MeshMetadata,
            CLodLevelInfosBufferId,
            Builtin::CLod::GroupPageMap,
            Builtin::CLod::StreamingRuntimeState,
            Builtin::CullingCameraBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerObjectBuffer,
            Builtin::CameraBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::Material::TextureGroup,
            Builtin::Material::TextureStreamingMetadataBuffer,
            Builtin::SkeletonResources::InverseBindMatrices,
            Builtin::SkeletonResources::BoneTransforms,
            Builtin::SkeletonResources::SkinningInstanceInfo,
            m_viewRasterInfoBuffer,
            m_voxelRasterQueueDescriptorResourceId.c_str())
        .WithUnorderedAccess(Builtin::Material::TextureStreamingFeedbackBuffer)
        .WithShaderResource(ECSResourceResolver(drawSetIndicesQuery))
        .WithIndirectArguments(m_pureComputeNodeDispatchArgsBuffer, m_pureComputeClusterDispatchArgsBuffer);

    if (UsesSWClassification(m_workGraphMode) && m_swVisibleClustersCounterBuffer) {
        builder->WithUnorderedAccess(m_swVisibleClustersCounterBuffer);
    }

    if (m_workGraphComputePageJobDescriptorsBuffer) {
        builder->WithShaderResource(m_workGraphComputePageJobDescriptorResourceId.c_str());
    }

    if (m_pageJobVisibleClustersBuffer && m_pageJobVisibleClustersCounterBuffer) {
        builder->WithUnorderedAccess(m_pageJobVisibleClustersBuffer, m_pageJobVisibleClustersCounterBuffer);
    }

    if (m_slabResourceGroup) {
        builder->WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
    }

    if (UsesVirtualShadowOutput(m_rasterOutputKind)) {
        builder->WithShaderResource(
            Builtin::Shadows::CLodClipmapInfo,
            Builtin::Shadows::CLodCompactShadowCameras);
        if (m_shadowDirtyHierarchyTexture) {
            builder->WithShaderResource(m_shadowDirtyHierarchyTexture);
        }
        if (m_shadowInvalidatedInstancesBitsetBuffer) {
            builder->WithShaderResource(m_shadowInvalidatedInstancesBitsetBuffer);
        }
        if (m_shadowPredictiveInvalidationCandidatesBuffer) {
            builder->WithUnorderedAccess(m_shadowPredictiveInvalidationCandidatesBuffer);
        }
        if (m_shadowPredictiveInvalidationCandidateCountBuffer) {
            builder->WithUnorderedAccess(m_shadowPredictiveInvalidationCandidateCountBuffer);
        }
        if (m_shadowPageTableTexture) {
            builder->WithUnorderedAccess(m_shadowPageTableTexture);
        }
        if (m_shadowPhysicalPagesTexture) {
            builder->WithUnorderedAccess(m_shadowPhysicalPagesTexture);
        }
    }

    if (UsesPerViewDepthMapOcclusion(m_rasterOutputKind)) {
        builder->WithUnorderedAccess(m_viewDepthSrvIndicesBuffer)
            .WithShaderResource(m_isFirstPass
                ? Builtin::LastFrameLinearDepthMaps
                : Builtin::PrimaryCamera::LinearDepthMap);
    }

    if (m_phase1VisibleClustersCounterBuffer && !m_isFirstPass) {
        builder->WithShaderResource(m_phase1VisibleClustersCounterBuffer);
    }
    if (m_swWriteBaseCounterBuffer && !m_isFirstPass) {
        builder->WithShaderResource(m_swWriteBaseCounterBuffer);
    }

    builder->WithConstantBuffer(Builtin::PerFrameBuffer);
}

void HierarchicalDispatchCullingPass::Setup()
{
}

PassReturn HierarchicalDispatchCullingPass::Execute(PassExecutionContext& executionContext)
{
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

    uint32_t sharedRootConstants[NumMiscUintRootConstants] = {};
    sharedRootConstants[CLOD_WG_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    sharedRootConstants[CLOD_WG_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    sharedRootConstants[CLOD_WG_FORCED_TRAVERSAL_DEPTH_ROOT] =
        SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodForceTraversalDepthRootSettingName)();
    sharedRootConstants[CLOD_WG_SW_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] =
        m_swVisibleClustersCounterBuffer
            ? m_swVisibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index
            : 0u;
    sharedRootConstants[CLOD_WG_TELEMETRY_DESCRIPTOR_INDEX] = m_workGraphTelemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    sharedRootConstants[CLOD_WG_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX] = m_occlusionReplayBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    sharedRootConstants[CLOD_WG_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX] = m_occlusionReplayStateBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    sharedRootConstants[CLOD_WG_WORKGRAPH_NODE_INPUTS_DESCRIPTOR_INDEX] = 0u;
    sharedRootConstants[CLOD_WG_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
    sharedRootConstants[CLOD_WG_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX] =
        UsesPerViewDepthMapOcclusion(m_rasterOutputKind)
            ? m_viewDepthSrvIndicesBuffer->GetSRVInfo(0).slot.index
            : 0u;
    sharedRootConstants[CLOD_WG_VISIBLE_CLUSTERS_CAPACITY] = static_cast<uint32_t>(m_maxVisibleClusters);
    sharedRootConstants[CLOD_WG_SHADOW_DIRTY_HIERARCHY_DESCRIPTOR_INDEX] =
        m_shadowDirtyHierarchyTexture
            ? m_shadowDirtyHierarchyTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index
            : 0u;
    sharedRootConstants[CLOD_WG_SHADOW_INVALIDATED_INSTANCES_DESCRIPTOR_INDEX] =
        m_shadowInvalidatedInstancesBitsetBuffer
            ? m_shadowInvalidatedInstancesBitsetBuffer->GetSRVInfo(0).slot.index
            : 0u;
    sharedRootConstants[CLOD_WG_SHADOW_PREDICTIVE_INVALIDATION_CANDIDATES_DESCRIPTOR_INDEX] =
        m_shadowPredictiveInvalidationCandidatesBuffer
            ? m_shadowPredictiveInvalidationCandidatesBuffer->GetUAVShaderVisibleInfo(0).slot.index
            : 0u;
    sharedRootConstants[CLOD_WG_SHADOW_PREDICTIVE_INVALIDATION_CANDIDATE_COUNT_DESCRIPTOR_INDEX] =
        m_shadowPredictiveInvalidationCandidateCountBuffer
            ? m_shadowPredictiveInvalidationCandidateCountBuffer->GetUAVShaderVisibleInfo(0).slot.index
            : 0u;
    uint32_t pageJobFlags = 0u;
    {
        auto& settings = SettingsManager::GetInstance();
        const bool pageJobEnabled =
            UsesVirtualShadowOutput(m_rasterOutputKind)
            && CLodVSMRasterModeUsesLargeClusterShadowRouting(
                settings.getSettingGetter<CLodVSMRasterMode>(CLodVSMRasterModeSettingName)());
        if (pageJobEnabled) {
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
    sharedRootConstants[CLOD_WG_PAGE_JOB_FLAGS] = pageJobFlags;
    sharedRootConstants[CLOD_WG_VIRTUAL_SHADOW_PAGE_TABLE_UAV_DESCRIPTOR_INDEX] =
        m_shadowPageTableTexture
            ? m_shadowPageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index
            : 0u;
    sharedRootConstants[CLOD_WG_VIRTUAL_SHADOW_PHYSICAL_PAGES_UAV_DESCRIPTOR_INDEX] =
        m_shadowPhysicalPagesTexture
            ? m_shadowPhysicalPagesTexture->GetUAVShaderVisibleInfo(0).slot.index
            : 0u;
    sharedRootConstants[CLOD_WG_HW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX] =
        (m_phase1VisibleClustersCounterBuffer ? m_phase1VisibleClustersCounterBuffer : m_visibleClustersCounterBuffer)
            ->GetSRVInfo(0)
            .slot.index;
    sharedRootConstants[CLOD_WG_SW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX] =
        (m_swWriteBaseCounterBuffer ? m_swWriteBaseCounterBuffer : m_swVisibleClustersCounterBuffer)
            ->GetSRVInfo(0)
            .slot.index;

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
    if (m_workGraphMode == HierarchicalCullingWorkGraphMode::SoftwareRasterCompute) {
        workGraphFlags |= CLOD_WG_FLAG_COMPUTE_SW_RASTER;
    }
    if (kDisableVirtualShadowDirtyPageCulling && UsesVirtualShadowOutput(m_rasterOutputKind)) {
        workGraphFlags |= CLOD_WG_FLAG_DISABLE_SHADOW_DIRTY_PAGE_CULLING;
    }
    constexpr uint32_t swRasterThreshold = 16u;
    workGraphFlags |= (swRasterThreshold << CLOD_WG_SW_RASTER_THRESHOLD_SHIFT);
    if (!m_isFirstPass) {
        workGraphFlags |= CLOD_WG_FLAG_PHASE2;
    }
    sharedRootConstants[CLOD_WG_FLAGS] = workGraphFlags;

    auto clearCounter = [&](const std::shared_ptr<Buffer>& buffer) {
        BindResourceDescriptorIndices(commandList, m_clearPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_clearPipelineState.GetAPIPipelineState().GetHandle());

        uint32_t clearRootConstants[NumMiscUintRootConstants] = {};
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = buffer->GetUAVShaderVisibleInfo(0).slot.index;
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
    };

    clearCounter(m_voxelRasterWorkCounterBuffer);

    auto bufferBarrier = [&](std::initializer_list<std::shared_ptr<Buffer>> buffers,
                             rhi::ResourceAccessType beforeAccess,
                             rhi::ResourceAccessType afterAccess,
                             rhi::ResourceSyncState beforeSync,
                             rhi::ResourceSyncState afterSync) {
        std::vector<rhi::BufferBarrier> barriers;
        barriers.reserve(buffers.size());
        for (const auto& buffer : buffers) {
            if (!buffer) {
                continue;
            }

            rhi::BufferBarrier barrier{};
            barrier.buffer = buffer->GetAPIResource().GetHandle();
            barrier.beforeAccess = beforeAccess;
            barrier.afterAccess = afterAccess;
            barrier.beforeSync = beforeSync;
            barrier.afterSync = afterSync;
            barriers.push_back(barrier);
        }

        if (!barriers.empty()) {
            rhi::BarrierBatch batch{};
            batch.buffers = rhi::Span<rhi::BufferBarrier>(barriers.data(), static_cast<uint32_t>(barriers.size()));
            commandList.Barriers(batch);
        }
    };

    auto uavBarrier = [&](std::initializer_list<std::shared_ptr<Buffer>> buffers) {
        bufferBarrier(
            buffers,
            rhi::ResourceAccessType::UnorderedAccess,
            rhi::ResourceAccessType::UnorderedAccess,
            rhi::ResourceSyncState::ComputeShading,
            rhi::ResourceSyncState::ComputeShading);
    };

    auto uavToComputeReadBarrier = [&](std::initializer_list<std::shared_ptr<Buffer>> buffers) {
        bufferBarrier(
            buffers,
            rhi::ResourceAccessType::UnorderedAccess,
            rhi::ResourceAccessType::ShaderResource,
            rhi::ResourceSyncState::ComputeShading,
            rhi::ResourceSyncState::ComputeShading);
    };

    auto uavToIndirectArgsBarrier = [&](std::initializer_list<std::shared_ptr<Buffer>> buffers) {
        bufferBarrier(
            buffers,
            rhi::ResourceAccessType::UnorderedAccess,
            rhi::ResourceAccessType::IndirectArgument,
            rhi::ResourceSyncState::ComputeShading,
            rhi::ResourceSyncState::ExecuteIndirect);
    };

    auto indirectArgsToUavBarrier = [&](std::initializer_list<std::shared_ptr<Buffer>> buffers) {
        bufferBarrier(
            buffers,
            rhi::ResourceAccessType::IndirectArgument,
            rhi::ResourceAccessType::UnorderedAccess,
            rhi::ResourceSyncState::ExecuteIndirect,
            rhi::ResourceSyncState::ComputeShading);
    };

    uavBarrier({ m_voxelRasterWorkCounterBuffer });

    bool nodeDispatchArgsNeedReuseBarrier = false;
    bool clusterDispatchArgsNeedReuseBarrier = false;

    auto buildDispatchArgs = [&](const std::shared_ptr<Buffer>& counterBuffer, const std::shared_ptr<Buffer>& argsBuffer, uint32_t threadsPerGroup) {
        BindResourceDescriptorIndices(commandList, m_pureComputeBuildDispatchArgsPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeBuildDispatchArgsPipelineState.GetAPIPipelineState().GetHandle());

        uint32_t dispatchRootConstants[NumMiscUintRootConstants] = {};
        std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(dispatchRootConstants));
        dispatchRootConstants[CLOD_WG_VISIBLE_CLUSTERS_CAPACITY] = static_cast<uint32_t>(m_maxVisibleClusters);
        dispatchRootConstants[CLOD_PC_DISPATCH_COUNTER_DESCRIPTOR_INDEX] = counterBuffer->GetSRVInfo(0).slot.index;
        dispatchRootConstants[CLOD_PC_DISPATCH_ARGS_DESCRIPTOR_INDEX] = argsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        dispatchRootConstants[CLOD_PC_DISPATCH_THREADS_PER_GROUP] = threadsPerGroup;
        dispatchRootConstants[CLOD_PC_DISPATCH_COUNT_LIMIT] = static_cast<uint32_t>(m_maxVisibleClusters);
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            dispatchRootConstants);
        commandList.Dispatch(1u, 1u, 1u);
    };

    auto buildDispatchArgsWithLimit = [&](const std::shared_ptr<Buffer>& counterBuffer,
                                          const std::shared_ptr<Buffer>& argsBuffer,
                                          uint32_t threadsPerGroup,
                                          uint32_t countLimit) {
        BindResourceDescriptorIndices(commandList, m_pureComputeBuildDispatchArgsPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeBuildDispatchArgsPipelineState.GetAPIPipelineState().GetHandle());

        uint32_t dispatchRootConstants[NumMiscUintRootConstants] = {};
        std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(dispatchRootConstants));
        dispatchRootConstants[CLOD_WG_VISIBLE_CLUSTERS_CAPACITY] = static_cast<uint32_t>(m_maxVisibleClusters);
        dispatchRootConstants[CLOD_PC_DISPATCH_COUNTER_DESCRIPTOR_INDEX] = counterBuffer->GetSRVInfo(0).slot.index;
        dispatchRootConstants[CLOD_PC_DISPATCH_ARGS_DESCRIPTOR_INDEX] = argsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        dispatchRootConstants[CLOD_PC_DISPATCH_THREADS_PER_GROUP] = threadsPerGroup;
        dispatchRootConstants[CLOD_PC_DISPATCH_COUNT_LIMIT] = countLimit;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            dispatchRootConstants);
        commandList.Dispatch(1u, 1u, 1u);
    };

    auto buildReplayDispatchArgs = [&](const std::shared_ptr<Buffer>& argsBuffer, uint32_t replaySourceKind, uint32_t threadsPerGroup) {
        BindResourceDescriptorIndices(commandList, m_pureComputeBuildReplayDispatchArgsPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeBuildReplayDispatchArgsPipelineState.GetAPIPipelineState().GetHandle());

        uint32_t dispatchRootConstants[NumMiscUintRootConstants] = {};
        dispatchRootConstants[CLOD_WG_VISIBLE_CLUSTERS_CAPACITY] = static_cast<uint32_t>(m_maxVisibleClusters);
        dispatchRootConstants[CLOD_WG_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX] =
            m_occlusionReplayStateBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        dispatchRootConstants[CLOD_PC_DISPATCH_ARGS_DESCRIPTOR_INDEX] = argsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        dispatchRootConstants[CLOD_PC_DISPATCH_THREADS_PER_GROUP] = threadsPerGroup;
        dispatchRootConstants[CLOD_PC_REPLAY_SOURCE_INDEX] = replaySourceKind;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            dispatchRootConstants);
        commandList.Dispatch(1u, 1u, 1u);
    };

    const bool useDenseClusterPath = true;
    const uint32_t denseClusterWorkCapacity = GetPureComputeDenseClusterWorkCapacity(m_maxVisibleClusters);

    auto dispatchClusterCull = [&](const std::shared_ptr<Buffer>& frontierBuffer, const std::shared_ptr<Buffer>& frontierCounterBuffer) {
        if (!useDenseClusterPath) {
            if (clusterDispatchArgsNeedReuseBarrier) {
                indirectArgsToUavBarrier({ m_pureComputeClusterDispatchArgsBuffer });
                clusterDispatchArgsNeedReuseBarrier = false;
            }
            buildDispatchArgs(frontierCounterBuffer, m_pureComputeClusterDispatchArgsBuffer, kPureComputeClusterThreadsPerGroup);
            uavToIndirectArgsBarrier({ m_pureComputeClusterDispatchArgsBuffer });

            BindResourceDescriptorIndices(commandList, m_pureComputeClusterPipelineState.GetResourceDescriptorSlots());
            commandList.BindPipeline(m_pureComputeClusterPipelineState.GetAPIPipelineState().GetHandle());
            uint32_t clusterRootConstants[NumMiscUintRootConstants] = {};
            std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(clusterRootConstants));
            clusterRootConstants[CLOD_PC_FRONTIER_INPUT_DESCRIPTOR_INDEX] = frontierBuffer->GetSRVInfo(0).slot.index;
            clusterRootConstants[CLOD_PC_FRONTIER_INPUT_COUNT_DESCRIPTOR_INDEX] = frontierCounterBuffer->GetSRVInfo(0).slot.index;
            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                clusterRootConstants);
            commandList.ExecuteIndirect(
                m_pureComputeDispatchCommandSignature->GetHandle(),
                m_pureComputeClusterDispatchArgsBuffer->GetAPIResource().GetHandle(),
                0,
                {},
                0,
                1);
            clusterDispatchArgsNeedReuseBarrier = true;
            return;
        }

        uavBarrier({ m_pureComputeDenseClusterWorkBuffer, m_pureComputeDenseClusterWorkCounterBuffer });
        clearCounter(m_pureComputeDenseClusterWorkCounterBuffer);
        uavBarrier({ m_pureComputeDenseClusterWorkCounterBuffer });

        if (clusterDispatchArgsNeedReuseBarrier) {
            indirectArgsToUavBarrier({ m_pureComputeClusterDispatchArgsBuffer });
            clusterDispatchArgsNeedReuseBarrier = false;
        }
        buildDispatchArgsWithLimit(frontierCounterBuffer, m_pureComputeClusterDispatchArgsBuffer, 1u, static_cast<uint32_t>(m_maxVisibleClusters));
        uavToIndirectArgsBarrier({ m_pureComputeClusterDispatchArgsBuffer });

        BindResourceDescriptorIndices(commandList, m_pureComputeExpandClustersPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeExpandClustersPipelineState.GetAPIPipelineState().GetHandle());
        uint32_t expandRootConstants[NumMiscUintRootConstants] = {};
        std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(expandRootConstants));
        expandRootConstants[CLOD_PC_FRONTIER_INPUT_DESCRIPTOR_INDEX] = frontierBuffer->GetSRVInfo(0).slot.index;
        expandRootConstants[CLOD_PC_FRONTIER_INPUT_COUNT_DESCRIPTOR_INDEX] = frontierCounterBuffer->GetSRVInfo(0).slot.index;
        expandRootConstants[CLOD_PC_CLUSTER_OUTPUT_DESCRIPTOR_INDEX] = m_pureComputeDenseClusterWorkBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        expandRootConstants[CLOD_PC_CLUSTER_OUTPUT_COUNT_DESCRIPTOR_INDEX] = m_pureComputeDenseClusterWorkCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            expandRootConstants);
        commandList.ExecuteIndirect(
            m_pureComputeDispatchCommandSignature->GetHandle(),
            m_pureComputeClusterDispatchArgsBuffer->GetAPIResource().GetHandle(),
            0,
            {},
            0,
            1);

        uavToComputeReadBarrier({ m_pureComputeDenseClusterWorkBuffer, m_pureComputeDenseClusterWorkCounterBuffer });

        indirectArgsToUavBarrier({ m_pureComputeClusterDispatchArgsBuffer });
        buildDispatchArgsWithLimit(
            m_pureComputeDenseClusterWorkCounterBuffer,
            m_pureComputeClusterDispatchArgsBuffer,
            kPureComputeDenseClusterThreadsPerGroup,
            denseClusterWorkCapacity);
        uavToIndirectArgsBarrier({ m_pureComputeClusterDispatchArgsBuffer });

        BindResourceDescriptorIndices(commandList, m_pureComputeDenseClusterPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeDenseClusterPipelineState.GetAPIPipelineState().GetHandle());
        uint32_t denseClusterRootConstants[NumMiscUintRootConstants] = {};
        std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(denseClusterRootConstants));
        denseClusterRootConstants[CLOD_PC_FRONTIER_INPUT_DESCRIPTOR_INDEX] = m_pureComputeDenseClusterWorkBuffer->GetSRVInfo(0).slot.index;
        denseClusterRootConstants[CLOD_PC_FRONTIER_INPUT_COUNT_DESCRIPTOR_INDEX] = m_pureComputeDenseClusterWorkCounterBuffer->GetSRVInfo(0).slot.index;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            denseClusterRootConstants);
        commandList.ExecuteIndirect(
            m_pureComputeDispatchCommandSignature->GetHandle(),
            m_pureComputeClusterDispatchArgsBuffer->GetAPIResource().GetHandle(),
            0,
            {},
            0,
            1);
        clusterDispatchArgsNeedReuseBarrier = true;
    };

    clearCounter(m_pureComputeCurrentNodeCounterBuffer);
    clearCounter(m_pureComputeNextNodeCounterBuffer);
    clearCounter(m_pureComputeClusterCounterBuffer);
    uavBarrier({ m_pureComputeCurrentNodeCounterBuffer, m_pureComputeNextNodeCounterBuffer, m_pureComputeClusterCounterBuffer });

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
                record.dispatchGridX = static_cast<uint>((count + kPureComputeObjectCullThreadsPerGroup - 1u) / kPureComputeObjectCullThreadsPerGroup);
                record.dispatchGridY = 1;
                record.dispatchGridZ = 1;
                cullRecords.push_back(record);
            }
        });

        BindResourceDescriptorIndices(commandList, m_pureComputeObjectCullPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeObjectCullPipelineState.GetAPIPipelineState().GetHandle());
        for (const ObjectCullRecord& record : cullRecords) {
            uint32_t objectCullRootConstants[NumMiscUintRootConstants] = {};
            std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(objectCullRootConstants));
            objectCullRootConstants[CLOD_PC_FRONTIER_OUTPUT_DESCRIPTOR_INDEX] = m_pureComputeCurrentNodeFrontierBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            objectCullRootConstants[CLOD_PC_FRONTIER_OUTPUT_COUNT_DESCRIPTOR_INDEX] = m_pureComputeCurrentNodeCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            objectCullRootConstants[CLOD_PC_OBJECT_CULL_ACTIVE_DRAW_COUNT] = record.activeDrawCount;
            objectCullRootConstants[CLOD_PC_OBJECT_CULL_VIEW_DATA_INDEX] = record.viewDataIndex;
            objectCullRootConstants[CLOD_PC_OBJECT_CULL_ACTIVE_DRAW_SET_SRV_INDEX] = record.activeDrawSetIndicesSRVIndex;
            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                objectCullRootConstants);
            commandList.Dispatch(record.dispatchGridX, record.dispatchGridY, record.dispatchGridZ);
        }

        uavToComputeReadBarrier({ m_pureComputeCurrentNodeFrontierBuffer, m_pureComputeCurrentNodeCounterBuffer });
    }
    else {
        sharedRootConstants[CLOD_WG_HW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX] =
            (m_phase1VisibleClustersCounterBuffer ? m_phase1VisibleClustersCounterBuffer : m_visibleClustersCounterBuffer)
                ->GetSRVInfo(0)
                .slot.index;
        sharedRootConstants[CLOD_WG_SW_WRITE_BASE_COUNTER_DESCRIPTOR_INDEX] =
            (m_swWriteBaseCounterBuffer ? m_swWriteBaseCounterBuffer : m_swVisibleClustersCounterBuffer)
                ->GetSRVInfo(0)
                .slot.index;

        buildReplayDispatchArgs(m_pureComputeNodeDispatchArgsBuffer, kReplaySourceNodes, kPureComputeTraverseThreadsPerGroup);
        uavToIndirectArgsBarrier({ m_pureComputeNodeDispatchArgsBuffer });

        BindResourceDescriptorIndices(commandList, m_pureComputeReplayNodesPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeReplayNodesPipelineState.GetAPIPipelineState().GetHandle());
        uint32_t replayNodeRootConstants[NumMiscUintRootConstants] = {};
        std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(replayNodeRootConstants));
        replayNodeRootConstants[CLOD_PC_FRONTIER_OUTPUT_DESCRIPTOR_INDEX] = m_pureComputeCurrentNodeFrontierBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        replayNodeRootConstants[CLOD_PC_FRONTIER_OUTPUT_COUNT_DESCRIPTOR_INDEX] = m_pureComputeCurrentNodeCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            replayNodeRootConstants);
        commandList.ExecuteIndirect(
            m_pureComputeDispatchCommandSignature->GetHandle(),
            m_pureComputeNodeDispatchArgsBuffer->GetAPIResource().GetHandle(),
            0,
            {},
            0,
            1);
        nodeDispatchArgsNeedReuseBarrier = true;

        buildReplayDispatchArgs(m_pureComputeClusterDispatchArgsBuffer, kReplaySourceClusters, kPureComputeClusterThreadsPerGroup);
        uavToIndirectArgsBarrier({ m_pureComputeClusterDispatchArgsBuffer });

        BindResourceDescriptorIndices(commandList, m_pureComputeReplayClustersPipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeReplayClustersPipelineState.GetAPIPipelineState().GetHandle());
        uint32_t replayClusterRootConstants[NumMiscUintRootConstants] = {};
        std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(replayClusterRootConstants));
        replayClusterRootConstants[CLOD_PC_CLUSTER_OUTPUT_DESCRIPTOR_INDEX] = m_pureComputeClusterFrontierBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        replayClusterRootConstants[CLOD_PC_CLUSTER_OUTPUT_COUNT_DESCRIPTOR_INDEX] = m_pureComputeClusterCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            replayClusterRootConstants);
        commandList.ExecuteIndirect(
            m_pureComputeDispatchCommandSignature->GetHandle(),
            m_pureComputeClusterDispatchArgsBuffer->GetAPIResource().GetHandle(),
            0,
            {},
            0,
            1);
        clusterDispatchArgsNeedReuseBarrier = true;

        uavToComputeReadBarrier({
            m_pureComputeCurrentNodeFrontierBuffer,
            m_pureComputeCurrentNodeCounterBuffer,
            m_pureComputeClusterFrontierBuffer,
            m_pureComputeClusterCounterBuffer,
        });

        dispatchClusterCull(m_pureComputeClusterFrontierBuffer, m_pureComputeClusterCounterBuffer);
    }

    uavToComputeReadBarrier({
        m_pureComputeCurrentNodeFrontierBuffer,
        m_pureComputeCurrentNodeCounterBuffer,
    });
    uavBarrier({
        m_visibleClustersCounterBuffer,
        m_occlusionReplayBuffer,
        m_occlusionReplayStateBuffer,
    });

    auto currentNodeFrontier = m_pureComputeCurrentNodeFrontierBuffer;
    auto currentNodeCounter = m_pureComputeCurrentNodeCounterBuffer;
    auto nextNodeFrontier = m_pureComputeNextNodeFrontierBuffer;
    auto nextNodeCounter = m_pureComputeNextNodeCounterBuffer;

    const uint32_t traversalLevelCount = std::min(m_activeTraversalDepth, kPureComputeMaxTraversalLevels);
    for (uint32_t level = 0; level < traversalLevelCount; ++level) {
        clearCounter(nextNodeCounter);
        clearCounter(m_pureComputeClusterCounterBuffer);
        uavBarrier({ nextNodeCounter, m_pureComputeClusterCounterBuffer });

        if (nodeDispatchArgsNeedReuseBarrier) {
            indirectArgsToUavBarrier({ m_pureComputeNodeDispatchArgsBuffer });
            nodeDispatchArgsNeedReuseBarrier = false;
        }
        buildDispatchArgs(currentNodeCounter, m_pureComputeNodeDispatchArgsBuffer, kPureComputeTraverseThreadsPerGroup);
        uavToIndirectArgsBarrier({ m_pureComputeNodeDispatchArgsBuffer });

        BindResourceDescriptorIndices(commandList, m_pureComputeTraversePipelineState.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_pureComputeTraversePipelineState.GetAPIPipelineState().GetHandle());
        uint32_t traverseRootConstants[NumMiscUintRootConstants] = {};
        std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(traverseRootConstants));
        traverseRootConstants[CLOD_PC_FRONTIER_INPUT_DESCRIPTOR_INDEX] = currentNodeFrontier->GetSRVInfo(0).slot.index;
        traverseRootConstants[CLOD_PC_FRONTIER_INPUT_COUNT_DESCRIPTOR_INDEX] = currentNodeCounter->GetSRVInfo(0).slot.index;
        traverseRootConstants[CLOD_PC_FRONTIER_OUTPUT_DESCRIPTOR_INDEX] = nextNodeFrontier->GetUAVShaderVisibleInfo(0).slot.index;
        traverseRootConstants[CLOD_PC_FRONTIER_OUTPUT_COUNT_DESCRIPTOR_INDEX] = nextNodeCounter->GetUAVShaderVisibleInfo(0).slot.index;
        traverseRootConstants[CLOD_PC_CLUSTER_OUTPUT_DESCRIPTOR_INDEX] = m_pureComputeClusterFrontierBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        traverseRootConstants[CLOD_PC_CLUSTER_OUTPUT_COUNT_DESCRIPTOR_INDEX] = m_pureComputeClusterCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            traverseRootConstants);
        commandList.ExecuteIndirect(
            m_pureComputeDispatchCommandSignature->GetHandle(),
            m_pureComputeNodeDispatchArgsBuffer->GetAPIResource().GetHandle(),
            0,
            {},
            0,
            1);
        nodeDispatchArgsNeedReuseBarrier = true;

        uavToComputeReadBarrier({
            nextNodeFrontier,
            nextNodeCounter,
            m_pureComputeClusterFrontierBuffer,
            m_pureComputeClusterCounterBuffer,
        });
        uavBarrier({
            m_occlusionReplayBuffer,
            m_occlusionReplayStateBuffer,
        });

        dispatchClusterCull(m_pureComputeClusterFrontierBuffer, m_pureComputeClusterCounterBuffer);

        uavBarrier({
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            m_pureComputeClusterFrontierBuffer,
            m_pureComputeDenseClusterWorkBuffer,
            m_pureComputeDenseClusterWorkCounterBuffer,
            m_occlusionReplayBuffer,
            m_occlusionReplayStateBuffer,
        });

        std::swap(currentNodeFrontier, nextNodeFrontier);
        std::swap(currentNodeCounter, nextNodeCounter);
    }

    uavToComputeReadBarrier({ m_visibleClustersCounterBuffer, m_occlusionReplayStateBuffer });

    BindResourceDescriptorIndices(commandList, m_createCommandPipelineState.GetResourceDescriptorSlots());
    commandList.BindPipeline(m_createCommandPipelineState.GetAPIPipelineState().GetHandle());

    uint32_t createRootConstants[NumMiscUintRootConstants] = {};
    std::copy(std::begin(sharedRootConstants), std::end(sharedRootConstants), std::begin(createRootConstants));
    createRootConstants[CLOD_CREATE_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
    createRootConstants[CLOD_CREATE_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX] = m_histogramIndirectCommand->GetUAVShaderVisibleInfo(0).slot.index;
    createRootConstants[CLOD_CREATE_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX] = m_occlusionReplayStateBuffer->GetSRVInfo(0).slot.index;
    createRootConstants[CLOD_CREATE_WORKGRAPH_NODE_INPUTS_DESCRIPTOR_INDEX] = m_occlusionNodeGpuInputsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    createRootConstants[CLOD_CREATE_NUM_RASTER_BUCKETS] = context.materialManager->GetRasterBucketCount();
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        createRootConstants);
    commandList.Dispatch(1u, 1u, 1u);

    return {};
}

void HierarchicalDispatchCullingPass::Update(const UpdateExecutionContext& executionContext)
{
    auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
    if (!updateContext) {
        return;
    }

    auto& context = *updateContext;
    m_declaredResourcesChanged = false;
    const std::vector<uint64_t> currentDrawSetResourceIds = CollectDeclaredDrawSetResourceIds(m_renderPhase, m_clodOnlyWorkloads);
    if (currentDrawSetResourceIds != m_declaredDrawSetResourceIds) {
        m_declaredDrawSetResourceIds = currentDrawSetResourceIds;
        m_declaredResourcesChanged = true;
    }
    m_activeTraversalDepth = context.meshManager != nullptr
        ? context.meshManager->GetCLodMaxTraversalDepth()
        : 0u;
    uint32_t zero = 0u;
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_visibleClustersCounterBuffer), 0);
    if (m_swVisibleClustersCounterBuffer) {
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_swVisibleClustersCounterBuffer), 0);
    }
    if (m_pageJobVisibleClustersCounterBuffer) {
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_pageJobVisibleClustersCounterBuffer), 0);
    }

    m_pureComputeCurrentNodeFrontierBuffer->ResizeStructured(std::max(1u, m_maxVisibleClusters));
    m_pureComputeNextNodeFrontierBuffer->ResizeStructured(std::max(1u, m_maxVisibleClusters));
    m_pureComputeClusterFrontierBuffer->ResizeStructured(std::max(1u, m_maxVisibleClusters));
    m_pureComputeCurrentNodeCounterBuffer->ResizeStructured(1u);
    m_pureComputeNextNodeCounterBuffer->ResizeStructured(1u);
    m_pureComputeClusterCounterBuffer->ResizeStructured(1u);
    m_pureComputeDenseClusterWorkBuffer->ResizeStructured(GetPureComputeDenseClusterWorkCapacity(m_maxVisibleClusters));
    m_pureComputeDenseClusterWorkCounterBuffer->ResizeStructured(1u);
    m_pureComputeNodeDispatchArgsBuffer->ResizeStructured(1u);
    m_pureComputeClusterDispatchArgsBuffer->ResizeStructured(1u);

    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_pureComputeCurrentNodeCounterBuffer), 0);
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_pureComputeNextNodeCounterBuffer), 0);
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_pureComputeClusterCounterBuffer), 0);
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_pureComputeDenseClusterWorkCounterBuffer), 0);

    auto numViews = context.viewManager->GetCameraBufferSize();
    std::vector<CLodViewRasterInfo> viewRasterInfo(numViews);
    context.viewManager->ForEachView([&](uint64_t viewID) {
        auto* viewInfo = context.viewManager->Get(viewID);
        if (!viewInfo) {
            return;
        }

        const auto cameraIndex = viewInfo->gpu.cameraBufferIndex;
        if (cameraIndex >= viewRasterInfo.size()) {
            return;
        }

        CLodViewRasterInfo info{};
        info.scissorMinX = 0;
        info.scissorMinY = 0;

        if (UsesVirtualShadowOutput(m_rasterOutputKind)) {
            const CLodVirtualShadowResolutionConfig virtualShadowConfig = CLodVirtualShadowBuildRuntimeResolutionConfig();
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
        }

        viewRasterInfo[cameraIndex] = info;
    });

    m_viewRasterInfoBuffer->ResizeStructured(static_cast<uint32_t>(viewRasterInfo.size()));
    BUFFER_UPLOAD(
        viewRasterInfo.data(),
        static_cast<uint32_t>(viewRasterInfo.size() * sizeof(CLodViewRasterInfo)),
        rg::runtime::UploadTarget::FromShared(m_viewRasterInfoBuffer),
        0);

    if (m_workGraphComputePageJobDescriptorsBuffer) {
        CLodWorkGraphComputePageJobDescriptors pageJobDescriptors{};
        pageJobDescriptors.visibleClustersUAVDescriptorIndex = m_pageJobVisibleClustersBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        pageJobDescriptors.visibleClustersCounterUAVDescriptorIndex =
            m_pageJobVisibleClustersCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        BUFFER_UPLOAD(
            &pageJobDescriptors,
            sizeof(CLodWorkGraphComputePageJobDescriptors),
            rg::runtime::UploadTarget::FromShared(m_workGraphComputePageJobDescriptorsBuffer),
            0);
    }

    CLodVoxelRasterQueueDescriptors voxelQueueDescriptors{};
    voxelQueueDescriptors.workRecordsUAVDescriptorIndex = m_voxelRasterWorkBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    voxelQueueDescriptors.workRecordCounterUAVDescriptorIndex = m_voxelRasterWorkCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    voxelQueueDescriptors.workRecordCapacity = m_voxelRasterWorkCapacity;
    BUFFER_UPLOAD(
        &voxelQueueDescriptors,
        sizeof(CLodVoxelRasterQueueDescriptors),
        rg::runtime::UploadTarget::FromShared(m_voxelRasterQueueDescriptorsBuffer),
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

    CLodNodeGpuInput nodeGpuInputs[3] = {};
    CLodMultiNodeGpuInput multiNodeGpuInput{};
    multiNodeGpuInput.numNodeInputs = 2;
    multiNodeGpuInput.pad0 = 0;
    multiNodeGpuInput.nodeInputStride = sizeof(CLodNodeGpuInput);

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

        nodeGpuInputs[1].entrypointIndex = 1;
        nodeGpuInputs[1].numRecords = 0;
        nodeGpuInputs[1].recordsAddress = replayAddress;
        nodeGpuInputs[1].recordStride = CLodNodeReplayStrideBytes;

        nodeGpuInputs[2].entrypointIndex = 2;
        nodeGpuInputs[2].numRecords = 0;
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

void HierarchicalDispatchCullingPass::Cleanup()
{
}

bool HierarchicalDispatchCullingPass::DeclaredResourcesChanged() const
{
    return m_declaredResourcesChanged;
}

std::shared_ptr<Resource> HierarchicalDispatchCullingPass::ProvideResource(ResourceIdentifier const& key)
{
    if (key == m_workGraphComputePageJobDescriptorResourceId) {
        return m_workGraphComputePageJobDescriptorsBuffer;
    }

    if (key == m_voxelRasterQueueDescriptorResourceId) {
        return m_voxelRasterQueueDescriptorsBuffer;
    }

    return nullptr;
}

std::vector<ResourceIdentifier> HierarchicalDispatchCullingPass::GetSupportedKeys()
{
    std::vector<ResourceIdentifier> resources{ ResourceIdentifier{ m_voxelRasterQueueDescriptorResourceId } };
    if (m_workGraphComputePageJobDescriptorsBuffer) {
        resources.emplace_back(m_workGraphComputePageJobDescriptorResourceId);
    }
    return resources;
}
