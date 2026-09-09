#include "Render/GraphExtensions/ClusterLOD/ReyesRasterWorkCompactAndArgsPass.h"

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "RenderPasses/PreparedComputeBarrier.h"
#include "Resources/Buffers/Buffer.h"
#include "../shaders/PerPassRootConstants/clodClearUintBufferRootConstants.h"
#include "../shaders/PerPassRootConstants/clodReyesRasterWorkBucketRootConstants.h"

ReyesRasterWorkCompactAndArgsPass::ReyesRasterWorkCompactAndArgsPass(
    std::shared_ptr<Buffer> rasterWorkBuffer,
    std::shared_ptr<Buffer> rasterWorkCounterBuffer,
    std::shared_ptr<Buffer> indirectCommand,
    std::shared_ptr<Buffer> histogramBuffer,
    std::shared_ptr<Buffer> offsetsBuffer,
    std::shared_ptr<Buffer> writeCursorBuffer,
    std::shared_ptr<Buffer> compactedRasterWorkIndicesBuffer,
    std::shared_ptr<Buffer> packedRasterWorkGroupsBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer)
    : m_rasterWorkBuffer(std::move(rasterWorkBuffer))
    , m_rasterWorkCounterBuffer(std::move(rasterWorkCounterBuffer))
    , m_indirectCommand(std::move(indirectCommand))
    , m_histogramBuffer(std::move(histogramBuffer))
    , m_offsetsBuffer(std::move(offsetsBuffer))
    , m_writeCursorBuffer(std::move(writeCursorBuffer))
    , m_compactedRasterWorkIndicesBuffer(std::move(compactedRasterWorkIndicesBuffer))
    , m_packedRasterWorkGroupsBuffer(std::move(packedRasterWorkGroupsBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer)) {
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/reyesRasterWorkBuckets.hlsl",
        L"CompactReyesRasterWorkCS",
        {},
        "CLod.ReyesRasterWorkCompactAndArgs.PSO");
    m_packPipeline = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/reyesRasterWorkBuckets.hlsl",
        L"EmitPackedReyesRasterWorkGroupsCS",
        {},
        "CLod.ReyesRasterWorkEmitPackedGroups.PSO");
    m_finalizePackPipeline = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/reyesRasterWorkBuckets.hlsl",
        L"PackReyesRasterWorkGroupsAndBuildIndirectArgsCS",
        {},
        "CLod.ReyesRasterWorkFinalizePackedGroups.PSO");
    m_clearPipeline = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUintStructuredBufferCSMain",
        {},
        "CLod.ReyesRasterWorkCompactClear.PSO");

    rhi::IndirectArg args[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 2 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    m_compactionCommandSignature = std::make_shared<rhi::CommandSignaturePtr>();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        *m_compactionCommandSignature);
}

void ReyesRasterWorkCompactAndArgsPass::Declare(org::PassBuilder& declaration) {
    declaration.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    auto* builder = &declaration;
    builder->WithShaderResource(
            m_rasterWorkBuffer,
            m_rasterWorkCounterBuffer,
            m_offsetsBuffer)
        .WithUnorderedAccess(
            m_histogramBuffer,
            m_writeCursorBuffer,
            m_compactedRasterWorkIndicesBuffer,
            m_packedRasterWorkGroupsBuffer,
            m_indirectArgsBuffer)
        .WithIndirectArguments(m_indirectCommand)
        .WithConstantBuffer(Builtin::PerFrameBuffer);
}

ReyesCompactFrameData ReyesRasterWorkCompactAndArgsPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto& context = *preparation.preparationData->Get<UpdateContext>();
    ReyesCompactFrameData data{};
    const auto numBuckets = context.preparedRasterBucketCount;
    if (numBuckets == 0u) return data;
    const auto capture = [&](auto& dispatch, const PipelineState& pipeline) {
        dispatch.resourceHeap = context.textureDescriptorHeap.GetHandle();
        dispatch.samplerHeap = context.samplerDescriptorHeap.GetHandle();
        auto binding = preparation.CaptureProgramBinding(pipeline);
        dispatch.program = binding.program;
        dispatch.descriptorIndices = std::move(binding.descriptorIndices);
    };
    capture(data.clear, m_clearPipeline);
    data.clear.constants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.clear.constants[CLOD_CLEAR_UINT_BUFFER_VALUE] = 0u;
    data.clear.constants[CLOD_CLEAR_UINT_BUFFER_COUNT] = numBuckets;
    data.clear.groupsX = (numBuckets + 63u) / 64u;
    capture(data.compact, m_pso);
    data.compact.commandSignature = preparation.CaptureCommandSignature(m_compactionCommandSignature);
    data.compact.argumentsReference = preparation.CaptureResource(m_indirectCommand->GetGlobalResourceID());
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_WORK_BUFFER_DESCRIPTOR_INDEX] = m_rasterWorkBuffer->GetSRVInfo(0).slot.index;
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_WORK_COUNTER_DESCRIPTOR_INDEX] = m_rasterWorkCounterBuffer->GetSRVInfo(0).slot.index;
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetSRVInfo(0).slot.index;
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_WRITE_CURSOR_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_COMPACTED_WORK_INDICES_DESCRIPTOR_INDEX] = m_compactedRasterWorkIndicesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_PACKED_WORK_GROUPS_DESCRIPTOR_INDEX] = m_packedRasterWorkGroupsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.compact.constants[CLOD_REYES_RASTER_BUCKET_NUM_BUCKETS] = numBuckets;
    data.pack = data.compact;
    capture(data.pack, m_packPipeline);
    capture(data.finalize, m_finalizePackPipeline);
    data.finalize.constants = data.compact.constants;
    data.finalize.groupsX = (numBuckets + 63u) / 64u;
    data.cursorBarrier = preparation.CaptureResource(m_writeCursorBuffer->GetGlobalResourceID());
    data.compactedBarrier = preparation.CaptureResource(m_compactedRasterWorkIndicesBuffer->GetGlobalResourceID());
    data.packedBarrier = preparation.CaptureResource(m_packedRasterWorkGroupsBuffer->GetGlobalResourceID());
    return data;
}

void ReyesRasterWorkCompactAndArgsPass::Record(const ReyesCompactFrameData& data, org::PassRecordContext& recording) {
    if (data.clear.groupsX == 0) return;
    br::render::RecordPreparedComputeDispatch(data.clear, recording);
    br::render::RecordPreparedComputeUavBarrier(data.cursorBarrier, recording);
    br::render::RecordPreparedComputeIndirect(data.compact, recording);
    br::render::RecordPreparedComputeUavBarrier(data.compactedBarrier, recording);
    br::render::RecordPreparedComputeIndirect(data.pack, recording);
    br::render::RecordPreparedComputeUavBarrier(data.packedBarrier, recording);
    br::render::RecordPreparedComputeDispatch(data.finalize, recording);
}

void ReyesRasterWorkCompactAndArgsPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    auto numBuckets = context.preparedRasterBucketCount;

    if (m_writeCursorBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
        m_writeCursorBuffer->ResizeStructured(numBuckets);
    }
    if (m_indirectArgsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(RasterizeClustersCommand)) {
        m_indirectArgsBuffer->ResizeStructured(numBuckets);
    }
}
