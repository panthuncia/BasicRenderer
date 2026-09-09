#include "Render/GraphExtensions/ClusterLOD/ReyesCreateDispatchArgsPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "../shaders/PerPassRootConstants/clodReyesCreateDispatchArgsRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

ReyesCreateDispatchArgsPass::ReyesCreateDispatchArgsPass(
    std::shared_ptr<Buffer> sourceCounterBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> sourceBaseCounterBuffer,
    uint32_t threadsPerGroup,
    uint32_t maxWorkItemCount)
    : m_sourceCounterBuffer(std::move(sourceCounterBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_sourceBaseCounterBuffer(std::move(sourceBaseCounterBuffer))
    , m_threadsPerGroup(threadsPerGroup)
    , m_maxWorkItemCount(maxWorkItemCount)
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"BuildReyesDispatchArgsCSMain",
        {},
        "CLod.ReyesCreateDispatchArgs.PSO");
}

void ReyesCreateDispatchArgsPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_sourceCounterBuffer)
        .WithUnorderedAccess(m_indirectArgsBuffer);
    if (m_sourceBaseCounterBuffer) {
        builder.WithShaderResource(m_sourceBaseCounterBuffer);
    }

    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}

void ReyesCreateDispatchArgsPass::Initialize()
{
}



void ReyesCreateDispatchArgsPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

void ReyesCreateDispatchArgsPass::ShutdownPass()
{
}

br::render::PreparedComputeDispatch ReyesCreateDispatchArgsPass::Prepare(const org::PassPrepareContext& preparation)
{
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    auto payload = m_pso.GetPayload();
    br::render::PreparedComputeDispatch data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    auto program = preparation.CaptureProgramBinding(std::move(payload));
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_REYES_CREATE_DISPATCH_ARGS_SOURCE_COUNTER_DESCRIPTOR_INDEX] = m_sourceCounterBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_REYES_CREATE_DISPATCH_ARGS_OUTPUT_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_REYES_CREATE_DISPATCH_ARGS_THREADS_PER_GROUP] = m_threadsPerGroup;
    data.constants[CLOD_REYES_CREATE_DISPATCH_ARGS_SOURCE_BASE_COUNTER_DESCRIPTOR_INDEX] = m_sourceBaseCounterBuffer
        ? m_sourceBaseCounterBuffer->GetSRVInfo(0).slot.index : 0xFFFFFFFFu;
    data.constants[CLOD_REYES_CREATE_DISPATCH_ARGS_MAX_WORK_ITEM_COUNT] = m_maxWorkItemCount;
    data.groupsX = 1;
    return data;
}

void ReyesCreateDispatchArgsPass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
