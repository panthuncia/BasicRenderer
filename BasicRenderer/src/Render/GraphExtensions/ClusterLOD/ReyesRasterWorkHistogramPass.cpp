#include "Render/GraphExtensions/ClusterLOD/ReyesRasterWorkHistogramPass.h"

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "../shaders/PerPassRootConstants/clodClearUintBufferRootConstants.h"
#include "../shaders/PerPassRootConstants/clodReyesRasterWorkBucketRootConstants.h"

ReyesRasterWorkHistogramPass::ReyesRasterWorkHistogramPass(
    std::shared_ptr<Buffer> rasterWorkBuffer,
    std::shared_ptr<Buffer> rasterWorkCounterBuffer,
    std::shared_ptr<Buffer> histogramIndirectCommand,
    std::shared_ptr<Buffer> histogramBuffer)
    : m_rasterWorkBuffer(std::move(rasterWorkBuffer))
    , m_rasterWorkCounterBuffer(std::move(rasterWorkCounterBuffer))
    , m_histogramIndirectCommand(std::move(histogramIndirectCommand))
    , m_histogramBuffer(std::move(histogramBuffer)) {
    CreatePipelines(
        DeviceManager::GetInstance().GetDevice(),
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_histogramPipeline,
        m_clearPipeline);

    rhi::IndirectArg histogramArgs[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(histogramArgs, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_histogramCommandSignature);
}

void ReyesRasterWorkHistogramPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithShaderResource(m_rasterWorkBuffer, m_rasterWorkCounterBuffer)
        .WithIndirectArguments(m_histogramIndirectCommand)
        .WithUnorderedAccess(m_histogramBuffer)
        .WithConstantBuffer(Builtin::PerFrameBuffer);
}

void ReyesRasterWorkHistogramPass::Setup() {}

PassReturn ReyesRasterWorkHistogramPass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;
    const uint32_t numRasterBuckets = context.materialManager->GetRasterBucketCount();
    if (numRasterBuckets == 0u) {
        return {};
    }

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

    BindResourceDescriptorIndices(commandList, m_clearPipeline.GetResourceDescriptorSlots());
    commandList.BindPipeline(m_clearPipeline.GetAPIPipelineState().GetHandle());

    uint32_t clearRootConstants[NumMiscUintRootConstants] = {};
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_VALUE] = 0u;
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] = numRasterBuckets;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        clearRootConstants);
    commandList.Dispatch((numRasterBuckets + 63u) / 64u, 1u, 1u);

    rhi::BufferBarrier histogramBarrier{};
    histogramBarrier.buffer = m_histogramBuffer->GetAPIResource().GetHandle();
    histogramBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    histogramBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    histogramBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
    histogramBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;

    rhi::BarrierBatch barrierBatch{};
    barrierBatch.buffers = { &histogramBarrier };
    commandList.Barriers(barrierBatch);

    commandList.BindPipeline(m_histogramPipeline.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_histogramPipeline.GetResourceDescriptorSlots());

    uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
    uintRootConstants[CLOD_REYES_RASTER_BUCKET_WORK_BUFFER_DESCRIPTOR_INDEX] = m_rasterWorkBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_RASTER_BUCKET_WORK_COUNTER_DESCRIPTOR_INDEX] = m_rasterWorkCounterBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_RASTER_BUCKET_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;

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

void ReyesRasterWorkHistogramPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    const auto numRasterBuckets = context.materialManager->GetRasterBucketCount();

    if (m_histogramBuffer->GetSize() < static_cast<size_t>(numRasterBuckets) * sizeof(uint32_t)) {
        m_histogramBuffer->ResizeStructured(numRasterBuckets);
    }
}

void ReyesRasterWorkHistogramPass::Cleanup() {}

void ReyesRasterWorkHistogramPass::CreatePipelines(
    rhi::Device device,
    rhi::PipelineLayoutHandle globalRootSignature,
    PipelineState& outHistogramPipeline,
    PipelineState& outClearPipeline)
{
    (void)device;
    outHistogramPipeline = PSOManager::GetInstance().MakeComputePipeline(
        globalRootSignature,
        L"Shaders/ClusterLOD/reyesRasterWorkBuckets.hlsl",
        L"HistogramReyesRasterWorkBucketsCS",
        {},
        "CLod.ReyesRasterWorkHistogram.PSO");
    outClearPipeline = PSOManager::GetInstance().MakeComputePipeline(
        globalRootSignature,
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUintStructuredBufferCSMain",
        {},
        "CLod.ReyesRasterWorkHistogramClear.PSO");
}
