#include "Render/GraphExtensions/ClusterLOD/ReyesCreateDispatchArgsPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "../shaders/PerPassRootConstants/clodReyesCreateDispatchArgsRootConstants.h"

ReyesCreateDispatchArgsPass::ReyesCreateDispatchArgsPass(
    std::shared_ptr<Buffer> sourceCounterBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> sourceBaseCounterBuffer,
    uint32_t threadsPerGroup)
    : m_sourceCounterBuffer(std::move(sourceCounterBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
    , m_sourceBaseCounterBuffer(std::move(sourceBaseCounterBuffer))
    , m_threadsPerGroup(threadsPerGroup)
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"BuildReyesDispatchArgsCSMain",
        {},
        "CLod.ReyesCreateDispatchArgs.PSO");
}

void ReyesCreateDispatchArgsPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_sourceCounterBuffer)
        .WithUnorderedAccess(m_indirectArgsBuffer);
    if (m_sourceBaseCounterBuffer) {
        builder->WithShaderResource(m_sourceBaseCounterBuffer);
    }
}

void ReyesCreateDispatchArgsPass::Setup()
{
}

PassReturn ReyesCreateDispatchArgsPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
    uintRootConstants[CLOD_REYES_CREATE_DISPATCH_ARGS_SOURCE_COUNTER_DESCRIPTOR_INDEX] = m_sourceCounterBuffer->GetSRVInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_CREATE_DISPATCH_ARGS_OUTPUT_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_CREATE_DISPATCH_ARGS_THREADS_PER_GROUP] = m_threadsPerGroup;
    uintRootConstants[CLOD_REYES_CREATE_DISPATCH_ARGS_SOURCE_BASE_COUNTER_DESCRIPTOR_INDEX] = m_sourceBaseCounterBuffer
        ? m_sourceBaseCounterBuffer->GetSRVInfo(0).slot.index
        : 0xFFFFFFFFu;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        uintRootConstants);

    commandList.Dispatch(1, 1, 1);
    return {};
}

void ReyesCreateDispatchArgsPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
}

void ReyesCreateDispatchArgsPass::Cleanup()
{
}