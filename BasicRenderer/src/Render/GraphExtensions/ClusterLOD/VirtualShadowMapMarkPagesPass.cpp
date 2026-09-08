#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapMarkPagesPass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "BuiltinResources.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "../shaders/PerPassRootConstants/clodClearUintBufferRootConstants.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowMarkBlocksRootConstants.h"
#include "Render/ShaderAPI.h"

VirtualShadowMapMarkPagesPass::VirtualShadowMapMarkPagesPass(
    std::shared_ptr<Buffer> tileWorkBuffer,
    std::shared_ptr<Buffer> tileCountBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> markClipmapDataBuffer,
    std::shared_ptr<Buffer> markedBlocksMaskBuffer,
    std::shared_ptr<Buffer> markedBlocksListBuffer,
    std::shared_ptr<Buffer> markedBlocksCountBuffer,
    std::shared_ptr<Buffer> receiverSubpageMaskBuffer)
    : m_tileWorkBuffer(std::move(tileWorkBuffer))
    , m_tileCountBuffer(std::move(tileCountBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_markClipmapDataBuffer(std::move(markClipmapDataBuffer))
    , m_markedBlocksMaskBuffer(std::move(markedBlocksMaskBuffer))
    , m_markedBlocksListBuffer(std::move(markedBlocksListBuffer))
    , m_markedBlocksCountBuffer(std::move(markedBlocksCountBuffer))
    , m_receiverSubpageMaskBuffer(std::move(receiverSubpageMaskBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowMarkBlocksCSMain",
        {},
        "CLod.VirtualShadow.MarkPages.PSO");

    m_clearPso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUintStructuredBufferCSMain",
        {},
        "CLod.VirtualShadow.MarkBlocks.Clear.PSO");
    m_clearUint2Pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUint2StructuredBufferCSMain",
        {},
        "CLod.VirtualShadow.ReceiverMask.ClearUint2.PSO");

    rhi::IndirectArg dispatchArgs[] = {
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    m_commandSignature = std::make_shared<rhi::CommandSignaturePtr>();
    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(dispatchArgs, 1), sizeof(CLodReyesDispatchIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        *m_commandSignature);
}

void VirtualShadowMapMarkPagesPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(
            Builtin::Shadows::CLodCompactMainCamera,
            m_tileWorkBuffer,
            m_tileCountBuffer,
            m_markClipmapDataBuffer
    )
        .WithIndirectArguments(m_indirectArgsBuffer)
        .WithUnorderedAccess(
            m_markedBlocksMaskBuffer,
            m_markedBlocksListBuffer,
            m_markedBlocksCountBuffer);
    if (m_receiverSubpageMaskBuffer) {
        builder->WithUnorderedAccess(m_receiverSubpageMaskBuffer);
    }
}

void VirtualShadowMapMarkPagesPass::Setup() {}

void VirtualShadowMapMarkPagesPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
    m_activeClipmapCount = (std::min)(
        static_cast<uint32_t>(SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")()),
    CLodVirtualShadowMaxSupportedClipmapCount);
    m_receiverSubpageMode = m_receiverSubpageMaskBuffer
        ? SettingsManager::GetInstance().getSettingGetter<uint32_t>(
            CLodDirectionalVirtualShadowReceiverSubpageModeSettingName)()
        : CLodVirtualShadowReceiverSubpageModeOff;
}

PassReturn VirtualShadowMapMarkPagesPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

    BindResourceDescriptorIndices(commandList, m_clearPso.GetResourceDescriptorSlots());
    commandList.BindPipeline(m_clearPso.GetAPIPipelineState().GetHandle());

    uint32_t clearRootConstants[NumMiscUintRootConstants] = {};
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_markedBlocksMaskBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_VALUE] = 0u;
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] = CLodVirtualShadowMaxMarkedBlockCount;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        clearRootConstants);
    commandList.Dispatch((CLodVirtualShadowMaxMarkedBlockCount + 63u) / 64u, 1u, 1u);

    if (m_receiverSubpageMode != CLodVirtualShadowReceiverSubpageModeOff) {
        const PipelineState& receiverClearPso =
            m_receiverSubpageMode == CLodVirtualShadowReceiverSubpageMode8x8
                ? m_clearUint2Pso
                : m_clearPso;
        BindResourceDescriptorIndices(commandList, receiverClearPso.GetResourceDescriptorSlots());
        commandList.BindPipeline(receiverClearPso.GetAPIPipelineState().GetHandle());
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] =
            m_receiverSubpageMaskBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] =
            CLodVirtualShadowMaxReceiverPageCount;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            clearRootConstants);
        commandList.Dispatch(
            (CLodVirtualShadowMaxReceiverPageCount + 63u) / 64u,
            1u,
            1u);
        BindResourceDescriptorIndices(commandList, m_clearPso.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_clearPso.GetAPIPipelineState().GetHandle());
    }

    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_markedBlocksCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    clearRootConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] = 1u;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        clearRootConstants);
    commandList.Dispatch(1u, 1u, 1u);

    rhi::BufferBarrier clearBarriers[3] = {};
    clearBarriers[0].buffer = m_markedBlocksMaskBuffer->GetAPIResource().GetHandle();
    clearBarriers[0].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    clearBarriers[0].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    clearBarriers[0].beforeSync = rhi::ResourceSyncState::ComputeShading;
    clearBarriers[0].afterSync = rhi::ResourceSyncState::ComputeShading;
    clearBarriers[1].buffer = m_markedBlocksCountBuffer->GetAPIResource().GetHandle();
    clearBarriers[1].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    clearBarriers[1].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    clearBarriers[1].beforeSync = rhi::ResourceSyncState::ComputeShading;
    clearBarriers[1].afterSync = rhi::ResourceSyncState::ComputeShading;
    uint32_t clearBarrierCount = 2u;
    if (m_receiverSubpageMode != CLodVirtualShadowReceiverSubpageModeOff) {
        clearBarriers[2].buffer =
            m_receiverSubpageMaskBuffer->GetAPIResource().GetHandle();
        clearBarriers[2].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        clearBarriers[2].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        clearBarriers[2].beforeSync = rhi::ResourceSyncState::ComputeShading;
        clearBarriers[2].afterSync = rhi::ResourceSyncState::ComputeShading;
        clearBarrierCount = 3u;
    }
    rhi::BarrierBatch barrierBatch{};
    barrierBatch.buffers =
        rhi::Span<rhi::BufferBarrier>(clearBarriers, clearBarrierCount);
    commandList.Barriers(barrierBatch);

    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_TILE_WORK_DESCRIPTOR_INDEX] = m_tileWorkBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_TILE_COUNT_DESCRIPTOR_INDEX] = m_tileCountBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_SCREEN_WIDTH] = context.renderResolution.x;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_SCREEN_HEIGHT] = context.renderResolution.y;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_ACTIVE_CLIPMAP_COUNT] = m_activeClipmapCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_CLIPMAP_DATA_DESCRIPTOR_INDEX] = m_markClipmapDataBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_MASK_DESCRIPTOR_INDEX] = m_markedBlocksMaskBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_LIST_DESCRIPTOR_INDEX] = m_markedBlocksListBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_COUNT_DESCRIPTOR_INDEX] = m_markedBlocksCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_RECEIVER_MASK_DESCRIPTOR_INDEX] =
        m_receiverSubpageMaskBuffer
            ? m_receiverSubpageMaskBuffer->GetUAVShaderVisibleInfo(0).slot.index
            : 0u;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_RECEIVER_MASK_ENABLED] =
        m_receiverSubpageMode;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    commandList.ExecuteIndirect((*m_commandSignature)->GetHandle(), m_indirectArgsBuffer->GetAPIResource().GetHandle(), 0, {}, 0, 1);

    return {};
}

PreparedPass VirtualShadowMapMarkPagesPass::PrepareFrame(FramePreparationContext& preparation)
{
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    PreparedData data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    data.clearOwner = m_clearPso.GetPayload();
    data.clearUint2Owner = m_clearUint2Pso.GetPayload();
    data.markOwner = m_pso.GetPayload();
    data.clearPipeline = data.clearOwner->pso.Get().GetHandle();
    data.clearUint2Pipeline = data.clearUint2Owner->pso.Get().GetHandle();
    data.markPipeline = data.markOwner->pso.Get().GetHandle();
    data.clearIndices = CaptureResourceDescriptorIndices(data.clearOwner->pipelineResources);
    data.clearUint2Indices = CaptureResourceDescriptorIndices(data.clearUint2Owner->pipelineResources);
    data.markIndices = CaptureResourceDescriptorIndices(data.markOwner->pipelineResources);
    data.commandSignatureOwner = m_commandSignature;
    data.commandSignature = (*m_commandSignature)->GetHandle();
    data.indirectArguments = m_indirectArgsBuffer->GetAPIResource().GetHandle();
    data.clearMask[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_markedBlocksMaskBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.clearMask[CLOD_CLEAR_UINT_BUFFER_COUNT] = CLodVirtualShadowMaxMarkedBlockCount;
    data.clearReceiver = data.clearMask;
    if (m_receiverSubpageMode != CLodVirtualShadowReceiverSubpageModeOff) {
        data.clearReceiver[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_receiverSubpageMaskBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        data.clearReceiver[CLOD_CLEAR_UINT_BUFFER_COUNT] = CLodVirtualShadowMaxReceiverPageCount;
        data.receiverGroups = (CLodVirtualShadowMaxReceiverPageCount + 63u) / 64u;
        data.receiverUint2 = m_receiverSubpageMode == CLodVirtualShadowReceiverSubpageMode8x8;
    }
    data.clearCount = data.clearMask;
    data.clearCount[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_markedBlocksCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.clearCount[CLOD_CLEAR_UINT_BUFFER_COUNT] = 1u;
    data.barrierResources[0] = m_markedBlocksMaskBuffer->GetAPIResource().GetHandle();
    data.barrierResources[1] = m_markedBlocksCountBuffer->GetAPIResource().GetHandle();
    if (data.receiverGroups) {
        data.barrierResources[2] = m_receiverSubpageMaskBuffer->GetAPIResource().GetHandle();
        data.barrierCount = 3;
    }
    auto& c = data.mark;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_TILE_WORK_DESCRIPTOR_INDEX] = m_tileWorkBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_TILE_COUNT_DESCRIPTOR_INDEX] = m_tileCountBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_SCREEN_WIDTH] = context->renderResolution.x;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_SCREEN_HEIGHT] = context->renderResolution.y;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_ACTIVE_CLIPMAP_COUNT] = m_activeClipmapCount;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_CLIPMAP_DATA_DESCRIPTOR_INDEX] = m_markClipmapDataBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_MASK_DESCRIPTOR_INDEX] = m_markedBlocksMaskBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_LIST_DESCRIPTOR_INDEX] = m_markedBlocksListBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_COUNT_DESCRIPTOR_INDEX] = m_markedBlocksCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_RECEIVER_MASK_DESCRIPTOR_INDEX] = m_receiverSubpageMaskBuffer ? m_receiverSubpageMaskBuffer->GetUAVShaderVisibleInfo(0).slot.index : 0u;
    c[CLOD_VIRTUAL_SHADOW_MARK_BLOCKS_RECEIVER_MASK_ENABLED] = m_receiverSubpageMode;
    return PreparedPass::MakeOwned(std::move(data), &RecordPrepared);
}

void VirtualShadowMapMarkPagesPass::RecordPrepared(const PreparedData& data, RecordingContext& recording)
{
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    const auto bind = [&](const std::vector<unsigned int>& indices) {
        if (!indices.empty()) commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(indices.size()), indices.data());
    };
    commands.BindPipeline(data.clearPipeline); bind(data.clearIndices);
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, data.clearMask.data());
    commands.Dispatch((CLodVirtualShadowMaxMarkedBlockCount + 63u) / 64u, 1u, 1u);
    if (data.receiverGroups) {
        commands.BindPipeline(data.receiverUint2 ? data.clearUint2Pipeline : data.clearPipeline);
        bind(data.receiverUint2 ? data.clearUint2Indices : data.clearIndices);
        commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, data.clearReceiver.data());
        commands.Dispatch(data.receiverGroups, 1u, 1u);
        commands.BindPipeline(data.clearPipeline); bind(data.clearIndices);
    }
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, data.clearCount.data());
    commands.Dispatch(1u, 1u, 1u);
    std::array<rhi::BufferBarrier, 3> barriers{};
    for (uint32_t i = 0; i < data.barrierCount; ++i) {
        barriers[i].buffer = data.barrierResources[i];
        barriers[i].beforeAccess = barriers[i].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        barriers[i].beforeSync = barriers[i].afterSync = rhi::ResourceSyncState::ComputeShading;
    }
    rhi::BarrierBatch batch{}; batch.buffers = {barriers.data(), data.barrierCount}; commands.Barriers(batch);
    commands.BindPipeline(data.markPipeline); bind(data.markIndices);
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, data.mark.data());
    commands.ExecuteIndirect(data.commandSignature, data.indirectArguments, 0, {}, 0, 1);
}

void VirtualShadowMapMarkPagesPass::Cleanup() {}
