#include "Render/GraphExtensions/ClusterLOD/CLodStreamingFeedbackSortPass.h"

#include <array>

#include <tracy/Tracy.hpp>

#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/PassBuilders.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "../shaders/PerPassRootConstants/clodStreamingFeedbackSortRootConstants.h"

CLodStreamingFeedbackSortPass::CLodStreamingFeedbackSortPass(
    std::shared_ptr<Buffer> requestKeys,
    std::shared_ptr<Buffer> requests,
    std::shared_ptr<Buffer> requestCounter,
    std::shared_ptr<Buffer> keyScratch,
    std::shared_ptr<Buffer> payloadScratch,
    std::shared_ptr<Buffer> sumTable,
    std::shared_ptr<Buffer> reduceTable,
    std::shared_ptr<Buffer> constants,
    std::shared_ptr<Buffer> countScatterArgs,
    std::shared_ptr<Buffer> reduceScanArgs)
    : m_requestKeys(std::move(requestKeys))
    , m_requests(std::move(requests))
    , m_requestCounter(std::move(requestCounter))
    , m_keyScratch(std::move(keyScratch))
    , m_payloadScratch(std::move(payloadScratch))
    , m_sumTable(std::move(sumTable))
    , m_reduceTable(std::move(reduceTable))
    , m_constants(std::move(constants))
    , m_countScatterArgs(std::move(countScatterArgs))
    , m_reduceScanArgs(std::move(reduceScanArgs)) {
    auto& psoManager = PSOManager::GetInstance();
    const auto computeRootSignature = psoManager.GetComputeRootSignature().GetHandle();
    constexpr const wchar_t* shaderPath = L"Shaders/FidelityFX/ParallelSort/clodStreamingFeedbackSort.hlsl";

    m_setupPso = psoManager.MakeComputePipeline(
        computeRootSignature,
        shaderPath,
        L"CLodStreamingFeedbackSortSetupCS",
        {},
        "CLod.StreamingFeedbackSort.Setup.PSO");
    m_countPso = psoManager.MakeComputePipeline(
        computeRootSignature,
        shaderPath,
        L"CLodStreamingFeedbackSortCountCS",
        {},
        "CLod.StreamingFeedbackSort.Count.PSO");
    m_reducePso = psoManager.MakeComputePipeline(
        computeRootSignature,
        shaderPath,
        L"CLodStreamingFeedbackSortReduceCS",
        {},
        "CLod.StreamingFeedbackSort.Reduce.PSO");
    m_scanPso = psoManager.MakeComputePipeline(
        computeRootSignature,
        shaderPath,
        L"CLodStreamingFeedbackSortScanCS",
        {},
        "CLod.StreamingFeedbackSort.Scan.PSO");
    m_scanAddPso = psoManager.MakeComputePipeline(
        computeRootSignature,
        shaderPath,
        L"CLodStreamingFeedbackSortScanAddCS",
        {},
        "CLod.StreamingFeedbackSort.ScanAdd.PSO");
    m_scatterPso = psoManager.MakeComputePipeline(
        computeRootSignature,
        shaderPath,
        L"CLodStreamingFeedbackSortScatterCS",
        {},
        "CLod.StreamingFeedbackSort.Scatter.PSO");
}

void CLodStreamingFeedbackSortPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithShaderResource(m_requestCounter)
        .WithUnorderedAccess(
            m_requestKeys,
            m_requests,
            m_keyScratch,
            m_payloadScratch,
            m_sumTable,
            m_reduceTable,
            m_constants,
            m_countScatterArgs,
            m_reduceScanArgs)
        .WithIndirectArguments(m_countScatterArgs, m_reduceScanArgs);
}

PassReturn CLodStreamingFeedbackSortPass::Execute(PassExecutionContext& executionContext) {
    ZoneScopedN("CLod::StreamingFeedbackSort");

    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;
    auto& psoManager = PSOManager::GetInstance();

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(psoManager.GetComputeRootSignature().GetHandle());
    const auto dispatchCommandSignature =
        CommandSignatureManager::GetInstance().GetRawDispatchCommandSignature().GetHandle();

    PushRootConstants(commandList, m_requestKeys, m_keyScratch, m_requests, m_payloadScratch, 0u);
    commandList.BindPipeline(m_setupPso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_setupPso.GetResourceDescriptorSlots());
    {
        ZoneScopedN("StreamingFeedbackSortSetup");
        commandList.Dispatch(1u, 1u, 1u);
    }

    UavBarrier(commandList);
    TransitionIndirectArgsForExecute(commandList);

    std::shared_ptr<Buffer> sourceKeys = m_requestKeys;
    std::shared_ptr<Buffer> destKeys = m_keyScratch;
    std::shared_ptr<Buffer> sourcePayloads = m_requests;
    std::shared_ptr<Buffer> destPayloads = m_payloadScratch;

    {
        ZoneScopedN("StreamingFeedbackSort");
        for (uint32_t iteration = 0u; iteration < 8u; ++iteration) {
            PushRootConstants(commandList, sourceKeys, destKeys, sourcePayloads, destPayloads, iteration);

            commandList.BindPipeline(m_countPso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_countPso.GetResourceDescriptorSlots());
            commandList.ExecuteIndirect(
                dispatchCommandSignature,
                m_countScatterArgs->GetAPIResource().GetHandle(),
                0,
                {},
                0,
                1);
            UavBarrier(commandList);

            commandList.BindPipeline(m_reducePso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_reducePso.GetResourceDescriptorSlots());
            commandList.ExecuteIndirect(
                dispatchCommandSignature,
                m_reduceScanArgs->GetAPIResource().GetHandle(),
                0,
                {},
                0,
                1);
            UavBarrier(commandList);

            commandList.BindPipeline(m_scanPso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_scanPso.GetResourceDescriptorSlots());
            commandList.Dispatch(1u, 1u, 1u);
            UavBarrier(commandList);

            commandList.BindPipeline(m_scanAddPso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_scanAddPso.GetResourceDescriptorSlots());
            commandList.ExecuteIndirect(
                dispatchCommandSignature,
                m_reduceScanArgs->GetAPIResource().GetHandle(),
                0,
                {},
                0,
                1);
            UavBarrier(commandList);

            commandList.BindPipeline(m_scatterPso.GetAPIPipelineState().GetHandle());
            BindResourceDescriptorIndices(commandList, m_scatterPso.GetResourceDescriptorSlots());
            commandList.ExecuteIndirect(
                dispatchCommandSignature,
                m_countScatterArgs->GetAPIResource().GetHandle(),
                0,
                {},
                0,
                1);
            UavBarrier(commandList);

            std::swap(sourceKeys, destKeys);
            std::swap(sourcePayloads, destPayloads);
        }
    }

    return {};
}

void CLodStreamingFeedbackSortPass::PushRootConstants(
    rhi::CommandList& commandList,
    const std::shared_ptr<Buffer>& sourceKeys,
    const std::shared_ptr<Buffer>& destKeys,
    const std::shared_ptr<Buffer>& sourcePayloads,
    const std::shared_ptr<Buffer>& destPayloads,
    uint32_t iterationIndex) const {
    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_STREAMING_SORT_REQUEST_COUNTER_DESCRIPTOR_INDEX] = m_requestCounter->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_CONSTANTS_DESCRIPTOR_INDEX] = m_constants->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_COUNT_SCATTER_ARGS_DESCRIPTOR_INDEX] = m_countScatterArgs->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_REDUCE_SCAN_ARGS_DESCRIPTOR_INDEX] = m_reduceScanArgs->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_SOURCE_KEYS_DESCRIPTOR_INDEX] = sourceKeys->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_DEST_KEYS_DESCRIPTOR_INDEX] = destKeys->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_SUM_TABLE_DESCRIPTOR_INDEX] = m_sumTable->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_REDUCE_TABLE_DESCRIPTOR_INDEX] = m_reduceTable->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_SOURCE_PAYLOADS_DESCRIPTOR_INDEX] = sourcePayloads->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_DEST_PAYLOADS_DESCRIPTOR_INDEX] = destPayloads->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_STREAMING_SORT_ITERATION_INDEX] = iterationIndex;
    rootConstants[CLOD_STREAMING_SORT_REQUEST_CAPACITY] = CLodStreamingRequestCapacity;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);
}

void CLodStreamingFeedbackSortPass::UavBarrier(rhi::CommandList& commandList) const {
    std::array<rhi::BufferBarrier, 7> barriers{};
    std::shared_ptr<Buffer> buffers[] = {
        m_requestKeys,
        m_requests,
        m_keyScratch,
        m_payloadScratch,
        m_sumTable,
        m_reduceTable,
        m_constants,
    };

    for (uint32_t i = 0; i < barriers.size(); ++i) {
        barriers[i].buffer = buffers[i]->GetAPIResource().GetHandle();
        barriers[i].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        barriers[i].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
        barriers[i].beforeSync = rhi::ResourceSyncState::ComputeShading;
        barriers[i].afterSync = rhi::ResourceSyncState::ComputeShading;
    }

    rhi::BarrierBatch barrierBatch{};
    barrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(barriers.data(), static_cast<uint32_t>(barriers.size()));
    commandList.Barriers(barrierBatch);
}

void CLodStreamingFeedbackSortPass::TransitionIndirectArgsForExecute(rhi::CommandList& commandList) const {
    rhi::BufferBarrier barriers[2] = {};
    barriers[0].buffer = m_countScatterArgs->GetAPIResource().GetHandle();
    barriers[1].buffer = m_reduceScanArgs->GetAPIResource().GetHandle();

    for (auto& barrier : barriers) {
        barrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        barrier.afterAccess = rhi::ResourceAccessType::IndirectArgument;
        barrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
        barrier.afterSync = rhi::ResourceSyncState::ExecuteIndirect;
    }

    rhi::BarrierBatch barrierBatch{};
    barrierBatch.buffers = rhi::Span<rhi::BufferBarrier>(barriers, 2);
    commandList.Barriers(barrierBatch);
}
