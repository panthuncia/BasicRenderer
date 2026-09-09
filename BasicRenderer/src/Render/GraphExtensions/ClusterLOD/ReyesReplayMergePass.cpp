#include "Render/GraphExtensions/ClusterLOD/ReyesReplayMergePass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "BuiltinResources.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodReyesReplayMergeRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

ReyesReplayMergePass::ReyesReplayMergePass(
    ReyesReplayMergeKind kind,
    std::shared_ptr<Buffer> sourceQueueBuffer,
    std::shared_ptr<Buffer> sourceQueueCounterBuffer,
    std::shared_ptr<Buffer> destQueueBuffer,
    std::shared_ptr<Buffer> destQueueCounterBuffer,
    std::shared_ptr<Buffer> destQueueOverflowBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> telemetryBuffer,
    uint32_t destQueueCapacity)
    : m_kind(kind)
    , m_sourceQueueBuffer(std::move(sourceQueueBuffer))
    , m_sourceQueueCounterBuffer(std::move(sourceQueueCounterBuffer))
    , m_destQueueBuffer(std::move(destQueueBuffer))
    , m_destQueueCounterBuffer(std::move(destQueueCounterBuffer))
    , m_destQueueOverflowBuffer(std::move(destQueueOverflowBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_telemetryBuffer(std::move(telemetryBuffer))
    , m_destQueueCapacity(destQueueCapacity)
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        kind == ReyesReplayMergeKind::Split ? L"MergeReyesReplaySplitQueueCSMain" : L"MergeReyesReplayDiceQueueCSMain",
        {},
        kind == ReyesReplayMergeKind::Split ? "CLod.ReyesReplayMergeSplit.PSO" : "CLod.ReyesReplayMergeDice.PSO");

    rhi::IndirectArg dispatchArgs[] = {
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    rhi::CommandSignaturePtr commandSignature;
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(dispatchArgs, 1), sizeof(CLodReyesDispatchIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        commandSignature);
    m_commandSignature = std::make_shared<rhi::CommandSignaturePtr>(std::move(commandSignature));
}

void ReyesReplayMergePass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_sourceQueueBuffer, m_sourceQueueCounterBuffer)
        .WithUnorderedAccess(m_destQueueBuffer, m_destQueueCounterBuffer, m_destQueueOverflowBuffer, m_telemetryBuffer)
        .WithConstantBuffer(Builtin::PerFrameBuffer);
    m_indirectArgumentsBinding = builder.BindIndirectArguments(m_indirectArgsBuffer);
}

void ReyesReplayMergePass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

br::render::PreparedComputeIndirect ReyesReplayMergePass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    br::render::PreparedComputeIndirect data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.commandSignature = preparation.CaptureCommandSignature(m_commandSignature);
    data.argumentsReference = preparation.CaptureResource(m_indirectArgumentsBinding);
    auto program = preparation.CaptureProgramBinding(m_pso);
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_REYES_REPLAY_MERGE_SOURCE_DESCRIPTOR_INDEX] = m_sourceQueueBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_REPLAY_MERGE_SOURCE_COUNTER_DESCRIPTOR_INDEX] = m_sourceQueueCounterBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_REPLAY_MERGE_DEST_DESCRIPTOR_INDEX] = m_destQueueBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_REPLAY_MERGE_DEST_COUNTER_DESCRIPTOR_INDEX] = m_destQueueCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_REPLAY_MERGE_DEST_OVERFLOW_DESCRIPTOR_INDEX] = m_destQueueOverflowBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_REPLAY_MERGE_CAPACITY] = m_destQueueCapacity;
    data.constants[CLOD_REYES_REPLAY_MERGE_TELEMETRY_DESCRIPTOR_INDEX] = m_telemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    return data;
}

void ReyesReplayMergePass::Record(const br::render::PreparedComputeIndirect& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeIndirect(data, recording);
}
