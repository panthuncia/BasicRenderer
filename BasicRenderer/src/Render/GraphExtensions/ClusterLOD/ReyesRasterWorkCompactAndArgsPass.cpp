#include "Render/GraphExtensions/ClusterLOD/ReyesRasterWorkCompactAndArgsPass.h"

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
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
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 2), sizeof(RasterBucketsHistogramIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_compactionCommandSignature);
}

void ReyesRasterWorkCompactAndArgsPass::DeclareResourceUsages(ComputePassBuilder* builder) {
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

void ReyesRasterWorkCompactAndArgsPass::Setup() {}

PassReturn ReyesRasterWorkCompactAndArgsPass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;
    auto& pm = PSOManager::GetInstance();

    const uint32_t numBuckets = context.materialManager->GetRasterBucketCount();
    if (numBuckets == 0u) {
        return {};
    }

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());

    BindResourceDescriptorIndices(commandList, m_clearPipeline.GetResourceDescriptorSlots());
    commandList.BindPipeline(m_clearPipeline.GetAPIPipelineState().GetHandle());

    uint32_t clearRootConstants[NumMiscUintRootConstants] = {};
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_VALUE] = 0u;
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] = numBuckets;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        clearRootConstants);
    commandList.Dispatch((numBuckets + 63u) / 64u, 1u, 1u);

    rhi::BufferBarrier writeCursorBarrier{};
    writeCursorBarrier.buffer = m_writeCursorBuffer->GetAPIResource().GetHandle();
    writeCursorBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    writeCursorBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    writeCursorBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
    writeCursorBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;

    rhi::BarrierBatch barrierBatch{};
    barrierBatch.buffers = { &writeCursorBarrier };
    commandList.Barriers(barrierBatch);

    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rc[NumMiscUintRootConstants] = {};
    rc[CLOD_REYES_RASTER_BUCKET_WORK_BUFFER_DESCRIPTOR_INDEX] = m_rasterWorkBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_REYES_RASTER_BUCKET_WORK_COUNTER_DESCRIPTOR_INDEX] = m_rasterWorkCounterBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_REYES_RASTER_BUCKET_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_REYES_RASTER_BUCKET_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_REYES_RASTER_BUCKET_WRITE_CURSOR_DESCRIPTOR_INDEX] = m_writeCursorBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_REYES_RASTER_BUCKET_COMPACTED_WORK_INDICES_DESCRIPTOR_INDEX] = m_compactedRasterWorkIndicesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_REYES_RASTER_BUCKET_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_REYES_RASTER_BUCKET_PACKED_WORK_GROUPS_DESCRIPTOR_INDEX] = m_packedRasterWorkGroupsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_REYES_RASTER_BUCKET_NUM_BUCKETS] = numBuckets;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rc);

    commandList.ExecuteIndirect(
        m_compactionCommandSignature->GetHandle(),
        m_indirectCommand->GetAPIResource().GetHandle(),
        0,
        {},
        0,
        1);

    rhi::BufferBarrier compactedWorkBarrier{};
    compactedWorkBarrier.buffer = m_compactedRasterWorkIndicesBuffer->GetAPIResource().GetHandle();
    compactedWorkBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    compactedWorkBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    compactedWorkBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
    compactedWorkBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;

    barrierBatch.buffers = { &compactedWorkBarrier };
    commandList.Barriers(barrierBatch);

    commandList.BindPipeline(m_packPipeline.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_packPipeline.GetResourceDescriptorSlots());
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rc);
    commandList.ExecuteIndirect(
        m_compactionCommandSignature->GetHandle(),
        m_indirectCommand->GetAPIResource().GetHandle(),
        0,
        {},
        0,
        1);

    rhi::BufferBarrier packedGroupsBarrier{};
    packedGroupsBarrier.buffer = m_packedRasterWorkGroupsBuffer->GetAPIResource().GetHandle();
    packedGroupsBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    packedGroupsBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    packedGroupsBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
    packedGroupsBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;

    barrierBatch.buffers = { &packedGroupsBarrier };
    commandList.Barriers(barrierBatch);

    commandList.BindPipeline(m_finalizePackPipeline.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_finalizePackPipeline.GetResourceDescriptorSlots());
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rc);
    commandList.Dispatch((numBuckets + 63u) / 64u, 1u, 1u);

    return {};
}

void ReyesRasterWorkCompactAndArgsPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    auto numBuckets = context.materialManager->GetRasterBucketCount();

    if (m_writeCursorBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
        m_writeCursorBuffer->ResizeStructured(numBuckets);
    }
    if (m_indirectArgsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(RasterizeClustersCommand)) {
        m_indirectArgsBuffer->ResizeStructured(numBuckets);
    }
}

void ReyesRasterWorkCompactAndArgsPass::Cleanup() {}
