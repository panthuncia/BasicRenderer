#include "Render/GraphExtensions/ClusterLOD/ReyesSeedPatchesPass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Resolvers/ResourceGroupResolver.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodReyesSeedRootConstants.h"
#include "Resources/Buffers/Buffer.h"
#include "RenderPasses/PreparedComputeDispatch.h"

ReyesSeedPatchesPass::ReyesSeedPatchesPass(
    std::shared_ptr<Buffer> visibleClustersBuffer,
    std::shared_ptr<Buffer> ownedClustersBuffer,
    std::shared_ptr<Buffer> ownedClustersCounterBuffer,
    std::shared_ptr<Buffer> splitQueueBuffer,
    std::shared_ptr<Buffer> splitQueueCounterBuffer,
    std::shared_ptr<Buffer> splitQueueOverflowBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<ResourceGroup> slabResourceGroup,
    uint32_t maxSplitQueueEntries,
    uint32_t phaseIndex)
    : m_visibleClustersBuffer(std::move(visibleClustersBuffer))
    , m_ownedClustersBuffer(std::move(ownedClustersBuffer))
    , m_ownedClustersCounterBuffer(std::move(ownedClustersCounterBuffer))
    , m_splitQueueBuffer(std::move(splitQueueBuffer))
    , m_splitQueueCounterBuffer(std::move(splitQueueCounterBuffer))
    , m_splitQueueOverflowBuffer(std::move(splitQueueOverflowBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_slabResourceGroup(std::move(slabResourceGroup))
    , m_maxSplitQueueEntries(maxSplitQueueEntries)
    , m_phaseIndex(phaseIndex) {
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/reyesSeedPatches.hlsl",
        L"ReyesSeedPatchesCS",
        {},
        "CLod.ReyesSeedPatches.PSO");

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

void ReyesSeedPatchesPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_visibleClustersBuffer, m_ownedClustersBuffer, m_ownedClustersCounterBuffer)
        .WithUnorderedAccess(m_splitQueueBuffer, m_splitQueueCounterBuffer, m_splitQueueOverflowBuffer);
    m_indirectArgumentsBinding = builder.BindIndirectArguments(m_indirectArgsBuffer);
    if (m_slabResourceGroup) {
        builder.WithShaderResource(ResourceGroupResolver(m_slabResourceGroup));
    }
}

br::render::PreparedComputeIndirect ReyesSeedPatchesPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    br::render::PreparedComputeIndirect data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.commandSignature = preparation.CaptureCommandSignature(m_commandSignature);
    data.argumentsReference = preparation.CaptureResource(m_indirectArgumentsBinding);
    auto program = preparation.CaptureProgramBinding(m_pso);
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_REYES_SEED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX] = m_visibleClustersBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_SEED_OWNED_CLUSTERS_DESCRIPTOR_INDEX] = m_ownedClustersBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_SEED_OWNED_CLUSTERS_COUNTER_DESCRIPTOR_INDEX] = m_ownedClustersCounterBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_SEED_OUTPUT_SPLIT_QUEUE_DESCRIPTOR_INDEX] = m_splitQueueBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_SEED_OUTPUT_SPLIT_QUEUE_COUNTER_DESCRIPTOR_INDEX] = m_splitQueueCounterBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_SEED_OUTPUT_SPLIT_QUEUE_OVERFLOW_DESCRIPTOR_INDEX] = m_splitQueueOverflowBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_SEED_QUEUE_CAPACITY] = m_maxSplitQueueEntries; data.constants[CLOD_REYES_SEED_PHASE_INDEX] = m_phaseIndex;
    return data;
}

void ReyesSeedPatchesPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

void ReyesSeedPatchesPass::Record(const br::render::PreparedComputeIndirect& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeIndirect(data, recording);
}
