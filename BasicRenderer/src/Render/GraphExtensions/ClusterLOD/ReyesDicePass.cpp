#include "Render/GraphExtensions/ClusterLOD/ReyesDicePass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodReyesDiceRootConstants.h"
#include "Resources/Buffers/Buffer.h"
#include "RenderPasses/PreparedComputeDispatch.h"

ReyesDicePass::ReyesDicePass(
    std::shared_ptr<Buffer> diceQueueBuffer,
    std::shared_ptr<Buffer> diceQueueCounterBuffer,
    std::shared_ptr<Buffer> diceQueueReadOffsetBuffer,
    std::shared_ptr<Buffer> tessTableConfigsBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> telemetryBuffer,
    uint32_t maxDiceQueueEntries,
    uint32_t phaseIndex)
    : m_diceQueueBuffer(std::move(diceQueueBuffer))
    , m_diceQueueCounterBuffer(std::move(diceQueueCounterBuffer))
    , m_diceQueueReadOffsetBuffer(std::move(diceQueueReadOffsetBuffer))
    , m_tessTableConfigsBuffer(std::move(tessTableConfigsBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_telemetryBuffer(std::move(telemetryBuffer))
    , m_maxDiceQueueEntries(maxDiceQueueEntries)
    , m_phaseIndex(phaseIndex) {
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/reyesDice.hlsl",
        L"ReyesDiceCS",
        {},
        "CLod.ReyesDice.PSO");

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

void ReyesDicePass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_diceQueueBuffer, m_diceQueueCounterBuffer, m_tessTableConfigsBuffer)
        .WithUnorderedAccess(m_telemetryBuffer);
    m_indirectArgumentsBinding = builder.BindIndirectArguments(m_indirectArgsBuffer);
    if (m_diceQueueReadOffsetBuffer) {
        builder.WithShaderResource(m_diceQueueReadOffsetBuffer);
    }
}

br::render::PreparedComputeIndirect ReyesDicePass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    br::render::PreparedComputeIndirect data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.commandSignature = preparation.CaptureCommandSignature(m_commandSignature);
    data.argumentsReference = preparation.CaptureResource(m_indirectArgumentsBinding);
    auto program = preparation.CaptureProgramBinding(m_pso);
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_REYES_DICE_QUEUE_READ_OFFSET_DESCRIPTOR_INDEX] = m_diceQueueReadOffsetBuffer ? m_diceQueueReadOffsetBuffer->GetSRVInfo(0).slot.index : 0xFFFFFFFFu;
    data.constants[CLOD_REYES_DICE_QUEUE_DESCRIPTOR_INDEX] = m_diceQueueBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_DICE_QUEUE_COUNTER_DESCRIPTOR_INDEX] = m_diceQueueCounterBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_DICE_TELEMETRY_DESCRIPTOR_INDEX] = m_telemetryBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_DICE_PHASE_INDEX] = m_phaseIndex; data.constants[CLOD_REYES_DICE_QUEUE_CAPACITY] = m_maxDiceQueueEntries;
    data.constants[CLOD_REYES_DICE_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX] = m_tessTableConfigsBuffer->GetSRVInfo(0).slot.index;
    return data;
}

void ReyesDicePass::Record(const br::render::PreparedComputeIndirect& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeIndirect(data, recording);
}
