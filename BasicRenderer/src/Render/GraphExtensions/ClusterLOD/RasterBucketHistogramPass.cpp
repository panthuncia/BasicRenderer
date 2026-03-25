#include "Render/GraphExtensions/ClusterLOD/RasterBucketHistogramPass.h"

#include <vector>

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "../shaders/PerPassRootConstants/clodHistogramRootConstants.h"

RasterBucketHistogramPass::RasterBucketHistogramPass(
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> visibleClustersCounterBuffer,
    std::shared_ptr<Buffer> histogramIndirectCommand,
    std::shared_ptr<Buffer> histogramBuffer,
    std::shared_ptr<Buffer> reyesOwnershipBitsetBuffer,
    std::shared_ptr<Buffer> readBaseCounterBuffer,
    bool readReverse,
    uint32_t visibleClustersCapacity,
    bool runWhenComputeSWRasterEnabledOnly) {
    CreatePipelines(
        DeviceManager::GetInstance().GetDevice(),
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_histogramPipeline);

    rhi::IndirectArg rasterizeClustersArgs[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(rasterizeClustersArgs, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(), m_histogramCommandSignature);

    m_visibleClustersBuffer = std::move(visibleClustersBuffer);
    m_visibleClustersCounterBuffer = std::move(visibleClustersCounterBuffer);
    m_histogramIndirectCommand = std::move(histogramIndirectCommand);
    m_histogramBuffer = std::move(histogramBuffer);
    m_reyesOwnershipBitsetBuffer = std::move(reyesOwnershipBitsetBuffer);
    m_readBaseCounterBuffer = std::move(readBaseCounterBuffer);
    m_readReverse = readReverse;
    m_visibleClustersCapacity = visibleClustersCapacity;
    m_runWhenComputeSWRasterEnabledOnly = runWhenComputeSWRasterEnabledOnly;
}

RasterBucketHistogramPass::~RasterBucketHistogramPass() = default;

void RasterBucketHistogramPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithShaderResource(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerMaterialDataBuffer)
        .WithIndirectArguments(m_histogramIndirectCommand)
        .WithUnorderedAccess(m_histogramBuffer);
    if (m_reyesOwnershipBitsetBuffer) {
        builder->WithShaderResource(m_reyesOwnershipBitsetBuffer);
    }
    if (m_readBaseCounterBuffer) {
        builder->WithShaderResource(m_readBaseCounterBuffer);
    }
}

void RasterBucketHistogramPass::Setup() {
    RegisterSRV(Builtin::PerMeshBuffer);
    RegisterSRV(Builtin::PerMeshInstanceBuffer);
    RegisterSRV(Builtin::PerMaterialDataBuffer);
}

PassReturn RasterBucketHistogramPass::Execute(PassExecutionContext& executionContext) {
    if (m_runWhenComputeSWRasterEnabledOnly && !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
        return {};
    }

    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

    commandList.BindPipeline(m_histogramPipeline.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_histogramPipeline.GetResourceDescriptorSlots());

    uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
    uintRootConstants[CLOD_HISTOGRAM_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_HISTOGRAM_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_HISTOGRAM_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    if (m_reyesOwnershipBitsetBuffer) {
        uintRootConstants[CLOD_HISTOGRAM_REYES_OWNERSHIP_BITSET_DESCRIPTOR_INDEX] = m_reyesOwnershipBitsetBuffer->GetSRVInfo(0).slot.index;
    }
    if (m_readBaseCounterBuffer) {
        uintRootConstants[CLOD_HISTOGRAM_READ_BASE_COUNTER_DESCRIPTOR_INDEX] = m_readBaseCounterBuffer->GetSRVInfo(0).slot.index;
    }
    uintRootConstants[CLOD_HISTOGRAM_READ_MODE_FLAGS] =
        (m_readReverse ? CLOD_HISTOGRAM_READ_FLAG_REVERSED : 0u) |
        (m_reyesOwnershipBitsetBuffer ? CLOD_HISTOGRAM_READ_FLAG_SKIP_REYES_OWNED : 0u);
    uintRootConstants[CLOD_HISTOGRAM_READ_CAPACITY] = m_visibleClustersCapacity;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        uintRootConstants);

    commandList.ExecuteIndirect(m_histogramCommandSignature->GetHandle(), m_histogramIndirectCommand->GetAPIResource().GetHandle(), 0, {}, 0, 1);

    return {};
}

void RasterBucketHistogramPass::Update(const UpdateExecutionContext& executionContext) {
    if (m_runWhenComputeSWRasterEnabledOnly && !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
        return;
    }

    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    auto numRasterBuckets = context.materialManager->GetRasterBucketCount();

    if (m_histogramBuffer->GetSize() < static_cast<size_t>(numRasterBuckets) * sizeof(uint32_t)) {
        m_histogramBuffer->ResizeStructured(numRasterBuckets);
    }

    std::vector<uint32_t> zeroData(numRasterBuckets, 0);
    BUFFER_UPLOAD(zeroData.data(), static_cast<uint32_t>(zeroData.size() * sizeof(uint32_t)), rg::runtime::UploadTarget::FromShared(m_histogramBuffer), 0);
}

void RasterBucketHistogramPass::Cleanup() {
}

void RasterBucketHistogramPass::CreatePipelines(
    rhi::Device device,
    rhi::PipelineLayoutHandle globalRootSignature,
    PipelineState& outHistogramPipeline)
{
    (void)device;
    outHistogramPipeline = PSOManager::GetInstance().MakeComputePipeline(
        globalRootSignature,
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"ClusterRasterBucketsHistogramCSMain");
}
