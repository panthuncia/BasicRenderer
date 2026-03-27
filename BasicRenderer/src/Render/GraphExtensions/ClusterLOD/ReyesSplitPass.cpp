#include "Render/GraphExtensions/ClusterLOD/ReyesSplitPass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "BuiltinResources.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodReyesSplitRootConstants.h"
#include "Resources/Buffers/Buffer.h"

ReyesSplitPass::ReyesSplitPass(
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> inputSplitQueueBuffer,
    std::shared_ptr<Buffer> inputSplitQueueCounterBuffer,
    std::shared_ptr<Buffer> outputSplitQueueBuffer,
    std::shared_ptr<Buffer> outputSplitQueueCounterBuffer,
    std::shared_ptr<Buffer> outputSplitQueueOverflowBuffer,
    std::shared_ptr<Buffer> diceQueueBuffer,
    std::shared_ptr<Buffer> diceQueueCounterBuffer,
    std::shared_ptr<Buffer> diceQueueOverflowBuffer,
    std::shared_ptr<Buffer> tessTableConfigsBuffer,
    std::shared_ptr<Buffer> tessTableVerticesBuffer,
    std::shared_ptr<Buffer> tessTableTrianglesBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> telemetryBuffer,
    uint32_t maxSplitQueueEntries,
    uint32_t splitPassIndex,
    uint32_t maxSplitPassCount,
    uint32_t phaseIndex)
    : m_visibleClustersBuffer(std::move(visibleClustersBuffer))
    , m_inputSplitQueueBuffer(std::move(inputSplitQueueBuffer))
    , m_inputSplitQueueCounterBuffer(std::move(inputSplitQueueCounterBuffer))
    , m_outputSplitQueueBuffer(std::move(outputSplitQueueBuffer))
    , m_outputSplitQueueCounterBuffer(std::move(outputSplitQueueCounterBuffer))
    , m_outputSplitQueueOverflowBuffer(std::move(outputSplitQueueOverflowBuffer))
    , m_diceQueueBuffer(std::move(diceQueueBuffer))
    , m_diceQueueCounterBuffer(std::move(diceQueueCounterBuffer))
    , m_diceQueueOverflowBuffer(std::move(diceQueueOverflowBuffer))
    , m_tessTableConfigsBuffer(std::move(tessTableConfigsBuffer))
    , m_tessTableVerticesBuffer(std::move(tessTableVerticesBuffer))
    , m_tessTableTrianglesBuffer(std::move(tessTableTrianglesBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_telemetryBuffer(std::move(telemetryBuffer))
    , m_maxSplitQueueEntries(maxSplitQueueEntries)
    , m_splitPassIndex(splitPassIndex)
    , m_maxSplitPassCount(maxSplitPassCount)
    , m_phaseIndex(phaseIndex) {
    m_clearCountersPso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearReyesSplitOutputCountersCSMain",
        {},
        "CLod.ReyesSplit.ClearCounters.PSO");

    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/reyesSplit.hlsl",
        L"ReyesSplitCS",
        {},
        "CLod.ReyesSplit.PSO");

    rhi::IndirectArg dispatchArgs[] = {
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(dispatchArgs, 1), sizeof(CLodReyesDispatchIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_commandSignature);
}

void ReyesSplitPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(
            m_visibleClustersBuffer,
            m_inputSplitQueueBuffer,
            m_inputSplitQueueCounterBuffer,
            m_tessTableConfigsBuffer,
            m_tessTableVerticesBuffer,
            m_tessTableTrianglesBuffer,
            Builtin::PerMeshInstanceBuffer,
            Builtin::PerObjectBuffer,
            Builtin::CullingCameraBuffer)
        .WithIndirectArguments(m_indirectArgsBuffer)
        .WithUnorderedAccess(
            m_outputSplitQueueBuffer,
            m_outputSplitQueueCounterBuffer,
            m_outputSplitQueueOverflowBuffer,
            m_diceQueueBuffer,
            m_diceQueueCounterBuffer,
            m_diceQueueOverflowBuffer,
            m_telemetryBuffer);
}

void ReyesSplitPass::Setup() {
    RegisterSRV(Builtin::PerMeshInstanceBuffer);
    RegisterSRV(Builtin::PerObjectBuffer);
    RegisterSRV(Builtin::CullingCameraBuffer);
}

PassReturn ReyesSplitPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

    uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
    uintRootConstants[CLOD_REYES_SPLIT_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_MAX_PASS_COUNT] = m_maxSplitPassCount;
    uintRootConstants[CLOD_REYES_SPLIT_INPUT_QUEUE_DESCRIPTOR_INDEX] = m_inputSplitQueueBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_INPUT_QUEUE_COUNTER_DESCRIPTOR_INDEX] = m_inputSplitQueueCounterBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_OUTPUT_SPLIT_QUEUE_DESCRIPTOR_INDEX] = m_outputSplitQueueBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_OUTPUT_SPLIT_QUEUE_COUNTER_DESCRIPTOR_INDEX] = m_outputSplitQueueCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_OUTPUT_SPLIT_QUEUE_OVERFLOW_DESCRIPTOR_INDEX] = m_outputSplitQueueOverflowBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_OUTPUT_DICE_QUEUE_DESCRIPTOR_INDEX] = m_diceQueueBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_OUTPUT_DICE_QUEUE_COUNTER_DESCRIPTOR_INDEX] = m_diceQueueCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_OUTPUT_DICE_QUEUE_OVERFLOW_DESCRIPTOR_INDEX] = m_diceQueueOverflowBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX] = m_tessTableConfigsBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_TESS_TABLE_VERTICES_DESCRIPTOR_INDEX] = m_tessTableVerticesBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_TESS_TABLE_TRIANGLES_DESCRIPTOR_INDEX] = m_tessTableTrianglesBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_SPLIT_QUEUE_CAPACITY] = m_maxSplitQueueEntries;

    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_clearCountersPso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_clearCountersPso.GetResourceDescriptorSlots());
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        uintRootConstants);
    commandList.Dispatch(1, 1, 1);

    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        uintRootConstants);

    commandList.ExecuteIndirect(m_commandSignature->GetHandle(), m_indirectArgsBuffer->GetAPIResource().GetHandle(), 0, {}, 0, 1);

    return {};
}

void ReyesSplitPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

void ReyesSplitPass::Cleanup() {}