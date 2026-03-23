#include "Render/GraphExtensions/ClusterLOD/ClusterSoftwareRasterizationPass.h"

#include <algorithm>

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "Render/RenderContext.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "BuiltinResources.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "../shaders/PerPassRootConstants/clodRasterizationRootConstants.h"

ClusterSoftwareRasterizationPass::ClusterSoftwareRasterizationPass(
    std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
    std::shared_ptr<Buffer> rasterBucketsHistogramBuffer,
    std::shared_ptr<Buffer> rasterBucketsIndirectArgsBuffer,
    std::shared_ptr<Buffer> sortedToUnsortedMappingBuffer,
    std::shared_ptr<Buffer> viewRasterInfoBuffer,
    std::shared_ptr<ResourceGroup> slabResourceGroup,
    bool runWhenComputeSWRasterEnabledOnly)
    : m_compactedVisibleClustersBuffer(std::move(compactedVisibleClustersBuffer))
    , m_rasterBucketsHistogramBuffer(std::move(rasterBucketsHistogramBuffer))
    , m_rasterBucketsIndirectArgsBuffer(std::move(rasterBucketsIndirectArgsBuffer))
    , m_sortedToUnsortedMappingBuffer(std::move(sortedToUnsortedMappingBuffer))
    , m_viewRasterInfoBuffer(std::move(viewRasterInfoBuffer))
    , m_slabResourceGroup(std::move(slabResourceGroup))
    , m_runWhenComputeSWRasterEnabledOnly(runWhenComputeSWRasterEnabledOnly) {
    rhi::IndirectArg args[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 3 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterizeClustersCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_rasterizationCommandSignature);
}

ClusterSoftwareRasterizationPass::~ClusterSoftwareRasterizationPass() = default;

void ClusterSoftwareRasterizationPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithShaderResource(
            Builtin::PerMeshBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerObjectBuffer,
            Builtin::CullingCameraBuffer,
            m_compactedVisibleClustersBuffer,
            m_rasterBucketsHistogramBuffer,
            m_sortedToUnsortedMappingBuffer,
            m_viewRasterInfoBuffer)
        .WithIndirectArguments(m_rasterBucketsIndirectArgsBuffer)
        .WithUnorderedAccess(Builtin::DebugVisualization);

    for (auto& vb : m_visibilityBuffers) {
        builder->WithUnorderedAccess(vb);
    }

    if (m_slabResourceGroup) {
        builder->WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
    }
}

void ClusterSoftwareRasterizationPass::Setup() {
    RegisterSRV(Builtin::PerMeshBuffer);
    RegisterSRV(Builtin::PerMaterialDataBuffer);
    RegisterSRV(Builtin::PerMeshInstanceBuffer);
    RegisterSRV(Builtin::PerObjectBuffer);
    RegisterSRV(Builtin::CullingCameraBuffer);
    RegisterUAV(Builtin::DebugVisualization);
}

void ClusterSoftwareRasterizationPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    std::vector<std::shared_ptr<PixelBuffer>> nextVisibilityBuffers;
    context.viewManager->ForEachView([&](uint64_t v) {
        auto viewInfo = context.viewManager->Get(v);
        if (viewInfo->gpu.visibilityBuffer != nullptr) {
            nextVisibilityBuffers.push_back(viewInfo->gpu.visibilityBuffer);
        }
    });

    m_declaredResourcesChanged = (nextVisibilityBuffers != m_visibilityBuffers);
    m_visibilityBuffers = std::move(nextVisibilityBuffers);
}

bool ClusterSoftwareRasterizationPass::DeclaredResourcesChanged() const {
    return m_declaredResourcesChanged;
}

PassReturn ClusterSoftwareRasterizationPass::Execute(PassExecutionContext& executionContext) {
    if (m_runWhenComputeSWRasterEnabledOnly && !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
        return {};
    }

    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

    uint32_t misc[NumMiscUintRootConstants] = {};
    misc[CLOD_RASTER_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_rasterBucketsHistogramBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_compactedVisibleClustersBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX] = m_viewRasterInfoBuffer->GetSRVInfo(0).slot.index;
    misc[CLOD_RASTER_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX] = m_sortedToUnsortedMappingBuffer->GetSRVInfo(0).slot.index;
    commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, misc);

    auto numBuckets = context.materialManager->GetRasterBucketCount();
    if (numBuckets == 0) {
        return {};
    }

    auto apiResource = m_rasterBucketsIndirectArgsBuffer->GetAPIResource();
    auto stride = sizeof(RasterizeClustersCommand);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto flags = context.materialManager->GetRasterFlagsForBucket(i);
        auto& pso = PSOManager::GetInstance().GetClusterLODSoftwareRasterPSO(flags);

        BindResourceDescriptorIndices(commandList, pso.GetResourceDescriptorSlots());
        commandList.BindPipeline(pso.GetAPIPipelineState().GetHandle());

        const uint64_t argOffset = static_cast<uint64_t>(i) * stride;
        commandList.ExecuteIndirect(
            m_rasterizationCommandSignature->GetHandle(),
            apiResource.GetHandle(),
            argOffset,
            {},
            0,
            1);
    }

    return {};
}

void ClusterSoftwareRasterizationPass::Cleanup() {}
