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

namespace {
struct PreparedStreamingFeedbackSort {
    struct Step {
        bool indirect = false;
        rhi::PipelineHandle pipeline{};
        std::shared_ptr<const org::PipelineStatePayload> pipelineOwner;
        std::vector<unsigned int> descriptors;
        std::array<unsigned int, NumMiscUintRootConstants> constants{};
        rhi::ResourceHandle arguments{};
    };
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    rhi::CommandSignatureHandle signature{};
    std::array<rhi::ResourceHandle, 7> uavResources{};
    std::array<rhi::ResourceHandle, 2> indirectResources{};
    std::vector<std::shared_ptr<const void>> resourceOwners;
    std::vector<Step> steps;
};

void RecordPreparedStreamingFeedbackSort(const PreparedStreamingFeedbackSort& data, org::RecordingContext& recording) {
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap); commands.BindLayout(data.layout);
    auto uavBarrier = [&] {
        std::array<rhi::BufferBarrier, 7> barriers{};
        for (size_t i = 0; i < barriers.size(); ++i) {
            barriers[i].buffer = data.uavResources[i];
            barriers[i].beforeAccess = barriers[i].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            barriers[i].beforeSync = barriers[i].afterSync = rhi::ResourceSyncState::ComputeShading;
        }
        rhi::BarrierBatch batch{}; batch.buffers = {barriers.data(), static_cast<uint32_t>(barriers.size())}; commands.Barriers(batch);
    };
    for (size_t index = 0; index < data.steps.size(); ++index) {
        const auto& step = data.steps[index];
        commands.BindPipeline(step.pipeline);
        if (!step.descriptors.empty()) commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(step.descriptors.size()), step.descriptors.data());
        commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
            NumMiscUintRootConstants, step.constants.data());
        if (step.indirect) commands.ExecuteIndirect(data.signature, step.arguments, 0, {}, 0, 1);
        else commands.Dispatch(1, 1, 1);
        uavBarrier();
        if (index == 0) {
            std::array<rhi::BufferBarrier, 2> barriers{};
            for (size_t i = 0; i < barriers.size(); ++i) {
                barriers[i].buffer = data.indirectResources[i];
                barriers[i].beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
                barriers[i].afterAccess = rhi::ResourceAccessType::IndirectArgument;
                barriers[i].beforeSync = rhi::ResourceSyncState::ComputeShading;
                barriers[i].afterSync = rhi::ResourceSyncState::ExecuteIndirect;
            }
            rhi::BarrierBatch batch{}; batch.buffers = {barriers.data(), static_cast<uint32_t>(barriers.size())}; commands.Barriers(batch);
        }
    }
}
}

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

PreparedPass CLodStreamingFeedbackSortPass::PrepareFrame(FramePreparationContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    PreparedStreamingFeedbackSort data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    data.signature = CommandSignatureManager::GetInstance().GetRawDispatchCommandSignature().GetHandle();
    const std::shared_ptr<Buffer> uav[] = {m_requestKeys, m_requests, m_keyScratch, m_payloadScratch, m_sumTable, m_reduceTable, m_constants};
    for (size_t i = 0; i < std::size(uav); ++i) { data.uavResources[i] = uav[i]->GetAPIResource().GetHandle(); data.resourceOwners.push_back(uav[i]); }
    data.indirectResources = {m_countScatterArgs->GetAPIResource().GetHandle(), m_reduceScanArgs->GetAPIResource().GetHandle()};
    data.resourceOwners.push_back(m_countScatterArgs); data.resourceOwners.push_back(m_reduceScanArgs);
    auto constants = [&](const std::shared_ptr<Buffer>& sourceKeys, const std::shared_ptr<Buffer>& destKeys,
        const std::shared_ptr<Buffer>& sourcePayloads, const std::shared_ptr<Buffer>& destPayloads, uint32_t iteration) {
        std::array<unsigned int, NumMiscUintRootConstants> c{};
        c[CLOD_STREAMING_SORT_REQUEST_COUNTER_DESCRIPTOR_INDEX] = m_requestCounter->GetSRVInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_CONSTANTS_DESCRIPTOR_INDEX] = m_constants->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_COUNT_SCATTER_ARGS_DESCRIPTOR_INDEX] = m_countScatterArgs->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_REDUCE_SCAN_ARGS_DESCRIPTOR_INDEX] = m_reduceScanArgs->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_SOURCE_KEYS_DESCRIPTOR_INDEX] = sourceKeys->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_DEST_KEYS_DESCRIPTOR_INDEX] = destKeys->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_SUM_TABLE_DESCRIPTOR_INDEX] = m_sumTable->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_REDUCE_TABLE_DESCRIPTOR_INDEX] = m_reduceTable->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_SOURCE_PAYLOADS_DESCRIPTOR_INDEX] = sourcePayloads->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_DEST_PAYLOADS_DESCRIPTOR_INDEX] = destPayloads->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_STREAMING_SORT_ITERATION_INDEX] = iteration; c[CLOD_STREAMING_SORT_REQUEST_CAPACITY] = CLodStreamingRequestCapacity;
        return c;
    };
    auto add = [&](const PipelineState& pipeline, bool indirect, rhi::ResourceHandle arguments,
        const std::array<unsigned int, NumMiscUintRootConstants>& c) {
        auto payload = pipeline.GetPayload(); PreparedStreamingFeedbackSort::Step step{};
        step.indirect = indirect; step.pipeline = payload->pso.Get().GetHandle(); step.pipelineOwner = std::move(payload);
        step.descriptors = CaptureResourceDescriptorIndices(step.pipelineOwner->pipelineResources);
        step.constants = c; step.arguments = arguments; data.steps.push_back(std::move(step));
    };
    auto sourceKeys = m_requestKeys, destKeys = m_keyScratch, sourcePayloads = m_requests, destPayloads = m_payloadScratch;
    add(m_setupPso, false, {}, constants(sourceKeys, destKeys, sourcePayloads, destPayloads, 0));
    for (uint32_t iteration = 0; iteration < 8; ++iteration) {
        const auto c = constants(sourceKeys, destKeys, sourcePayloads, destPayloads, iteration);
        add(m_countPso, true, data.indirectResources[0], c); add(m_reducePso, true, data.indirectResources[1], c);
        add(m_scanPso, false, {}, c); add(m_scanAddPso, true, data.indirectResources[1], c);
        add(m_scatterPso, true, data.indirectResources[0], c);
        std::swap(sourceKeys, destKeys); std::swap(sourcePayloads, destPayloads);
    }
    return PreparedPass::MakeOwned(std::move(data), &RecordPreparedStreamingFeedbackSort);
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
