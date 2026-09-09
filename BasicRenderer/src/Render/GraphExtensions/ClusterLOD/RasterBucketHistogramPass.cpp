#include "Render/GraphExtensions/ClusterLOD/RasterBucketHistogramPass.h"

#include <stdexcept>
#include <vector>

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "../shaders/PerPassRootConstants/clodClearUintBufferRootConstants.h"
#include "../shaders/PerPassRootConstants/clodHistogramRootConstants.h"

RasterBucketHistogramPass::RasterBucketHistogramPass(
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> visibleClustersCounterBuffer,
    std::shared_ptr<Buffer> histogramIndirectCommand,
    std::shared_ptr<Buffer> histogramBuffer,
    std::shared_ptr<Buffer> reyesOwnershipBitsetBuffer,
    std::shared_ptr<Buffer> telemetryBuffer,
    std::shared_ptr<Buffer> readBaseCounterBuffer,
    bool readReverse,
    uint32_t visibleClustersCapacity,
    bool runWhenComputeSWRasterEnabledOnly) {
    if (visibleClustersCapacity == 0u) {
        throw std::invalid_argument("RasterBucketHistogramPass requires a non-zero visible-cluster capacity");
    }
    CreatePipelines(
        DeviceManager::GetInstance().GetDevice(),
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_histogramPipeline,
        m_clearPipeline);

    rhi::IndirectArg rasterizeClustersArgs[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    rhi::CommandSignaturePtr histogramCommandSignature;
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(rasterizeClustersArgs, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(), histogramCommandSignature);
    m_histogramCommandSignature = std::make_shared<rhi::CommandSignaturePtr>(std::move(histogramCommandSignature));

    m_visibleClustersBuffer = std::move(visibleClustersBuffer);
    m_visibleClustersCounterBuffer = std::move(visibleClustersCounterBuffer);
    m_histogramIndirectCommand = std::move(histogramIndirectCommand);
    m_histogramBuffer = std::move(histogramBuffer);
    m_reyesOwnershipBitsetBuffer = std::move(reyesOwnershipBitsetBuffer);
    m_telemetryBuffer = std::move(telemetryBuffer);
    m_readBaseCounterBuffer = std::move(readBaseCounterBuffer);
    m_readReverse = readReverse;
    m_visibleClustersCapacity = visibleClustersCapacity;
    m_runWhenComputeSWRasterEnabledOnly = runWhenComputeSWRasterEnabledOnly;
}

RasterBucketHistogramPass::~RasterBucketHistogramPass() = default;

void RasterBucketHistogramPass::Declare(org::PassBuilder& builder) {
    builder.WithShaderResource(
            m_visibleClustersBuffer,
            m_visibleClustersCounterBuffer,
            Builtin::PerMeshBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::InstanceDrawRecordBuffer,
            Builtin::PerInstanceTransformBuffer,
            Builtin::PerMaterialDataBuffer,
            Builtin::Material::TextureStreamingMetadataBuffer)
        .WithIndirectArguments(m_histogramIndirectCommand)
    		.WithUnorderedAccess(m_histogramBuffer, Builtin::Material::TextureStreamingFeedbackBuffer);
    if (m_reyesOwnershipBitsetBuffer) {
        builder.WithShaderResource(m_reyesOwnershipBitsetBuffer);
    }
    if (m_telemetryBuffer) {
        builder.WithUnorderedAccess(m_telemetryBuffer);
    }
    if (m_readBaseCounterBuffer) {
        builder.WithShaderResource(m_readBaseCounterBuffer);
    }

    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}


RasterBucketHistogramPreparedData RasterBucketHistogramPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    PreparedData data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    data.clearProgram = preparation.CaptureProgram(m_clearPipeline);
    data.histogramProgram = preparation.CaptureProgram(m_histogramPipeline);
    preparation.Retain(m_histogramCommandSignature);
    data.commandSignature = (*m_histogramCommandSignature)->GetHandle();
    data.indirectArguments = preparation.CaptureResource(m_histogramIndirectCommand->GetGlobalResourceID());
    data.histogramResource = preparation.CaptureResource(m_histogramBuffer->GetGlobalResourceID());
    data.clearDescriptorIndices = CaptureResourceDescriptorIndices(m_clearPipeline.GetResourceDescriptorSlots());
    data.histogramDescriptorIndices = CaptureResourceDescriptorIndices(m_histogramPipeline.GetResourceDescriptorSlots());
    data.clearConstants.resize(NumMiscUintRootConstants);
    data.histogramConstants.resize(NumMiscUintRootConstants);
    const uint32_t numBuckets = context->materialManager->GetRasterBucketCount();
    data.enabled = numBuckets != 0u && (!m_runWhenComputeSWRasterEnabledOnly
        || CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)()));
    data.clearGroups = (numBuckets + 63u) / 64u;
    data.clearConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.clearConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] = numBuckets;
    auto& c = data.histogramConstants;
    c[CLOD_HISTOGRAM_READ_BASE_COUNTER_DESCRIPTOR_INDEX] = m_readBaseCounterBuffer ? m_readBaseCounterBuffer->GetSRVInfo(0).slot.index : 0xFFFFFFFFu;
    c[CLOD_HISTOGRAM_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_HISTOGRAM_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_visibleClustersCounterBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_HISTOGRAM_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_HISTOGRAM_TELEMETRY_DESCRIPTOR_INDEX] = m_telemetryBuffer && IsCLodWorkGraphTelemetryEnabled() ? m_telemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index : 0xFFFFFFFFu;
    c[CLOD_HISTOGRAM_REYES_OWNERSHIP_BITSET_DESCRIPTOR_INDEX] = m_reyesOwnershipBitsetBuffer ? m_reyesOwnershipBitsetBuffer->GetSRVInfo(0).slot.index : 0xFFFFFFFFu;
    c[CLOD_HISTOGRAM_NUM_RASTER_BUCKETS] = numBuckets;
    c[CLOD_HISTOGRAM_READ_MODE_FLAGS] = (m_readReverse ? CLOD_HISTOGRAM_READ_FLAG_REVERSED : 0u)
        | (m_reyesOwnershipBitsetBuffer ? CLOD_HISTOGRAM_READ_FLAG_SKIP_REYES_OWNED : 0u);
    c[CLOD_HISTOGRAM_READ_CAPACITY] = m_visibleClustersCapacity;
    return data;
}

void RasterBucketHistogramPass::Record(const PreparedData& data, org::PassRecordContext& recording) {
    if (!data.enabled) return;
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    auto bindIndices = [&](const std::vector<unsigned int>& indices) {
        if (!indices.empty()) commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(indices.size()), indices.data());
    };
    commands.BindPipeline(recording.Resolve(data.clearProgram));
    bindIndices(data.clearDescriptorIndices);
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
        NumMiscUintRootConstants, data.clearConstants.data());
    commands.Dispatch(data.clearGroups, 1, 1);
    rhi::BufferBarrier barrier{};
    barrier.buffer = recording.Resolve(data.histogramResource).GetHandle();
    barrier.beforeAccess = barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    barrier.beforeSync = barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
    rhi::BarrierBatch barriers{}; barriers.buffers = {&barrier}; commands.Barriers(barriers);
    commands.BindPipeline(recording.Resolve(data.histogramProgram));
    bindIndices(data.histogramDescriptorIndices);
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
        NumMiscUintRootConstants, data.histogramConstants.data());
    commands.ExecuteIndirect(data.commandSignature,
        recording.Resolve(data.indirectArguments).GetHandle(), 0, {}, 0, 1);
}

void RasterBucketHistogramPass::Update(const UpdateExecutionContext& executionContext) {
    if (m_runWhenComputeSWRasterEnabledOnly && !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
        return;
    }

    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    auto numRasterBuckets = context.preparedRasterBucketCount;

    if (m_histogramBuffer->GetSize() < static_cast<size_t>(numRasterBuckets) * sizeof(uint32_t)) {
        m_histogramBuffer->ResizeStructured(numRasterBuckets);
    }

}

void RasterBucketHistogramPass::CreatePipelines(
    rhi::Device device,
    rhi::PipelineLayoutHandle globalRootSignature,
    PipelineState& outHistogramPipeline,
    PipelineState& outClearPipeline)
{
    (void)device;
    outHistogramPipeline = PSOManager::GetInstance().MakeComputePipeline(
        globalRootSignature,
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"ClusterRasterBucketsHistogramCSMain");
    outClearPipeline = PSOManager::GetInstance().MakeComputePipeline(
        globalRootSignature,
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUintStructuredBufferCSMain");
}
