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

void CLodStreamingFeedbackSortPass::Declare(org::PassBuilder& declaration) {
    declaration.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    auto* builder = &declaration;
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

StreamingFeedbackSortFrameData CLodStreamingFeedbackSortPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    StreamingFeedbackSortFrameData data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.signature = preparation.CaptureCommandSignature(CommandSignatureManager::GetInstance().CaptureRawDispatchCommandSignature());
    data.programs = {preparation.CaptureProgramBinding(m_setupPso), preparation.CaptureProgramBinding(m_countPso),
        preparation.CaptureProgramBinding(m_reducePso), preparation.CaptureProgramBinding(m_scanPso),
        preparation.CaptureProgramBinding(m_scanAddPso), preparation.CaptureProgramBinding(m_scatterPso)};
    const std::shared_ptr<Buffer> uav[] = {m_requestKeys, m_requests, m_keyScratch, m_payloadScratch, m_sumTable, m_reduceTable, m_constants};
    for (size_t i = 0; i < std::size(uav); ++i)
        data.uavResources[i] = preparation.CaptureResource(uav[i]->GetGlobalResourceID());
    data.indirectResources = {preparation.CaptureResource(m_countScatterArgs->GetGlobalResourceID()),
        preparation.CaptureResource(m_reduceScanArgs->GetGlobalResourceID())};
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
    data.constants[0] = constants(m_requestKeys, m_keyScratch, m_requests, m_payloadScratch, 0);
    data.constants[1] = constants(m_keyScratch, m_requestKeys, m_payloadScratch, m_requests, 0);
    return data;
}

void CLodStreamingFeedbackSortPass::Record(const StreamingFeedbackSortFrameData& data, org::PassRecordContext& recording) {
    auto& commands = recording.Commands();
    br::render::BindPreparedDescriptorHeaps(commands, data.resourceHeap, data.samplerHeap);
    const auto uavBarrier = [&] {
        std::array<rhi::BufferBarrier, 7> barriers{};
        for (size_t i = 0; i < barriers.size(); ++i) {
            barriers[i].buffer = recording.Resolve(data.uavResources[i]).GetHandle();
            barriers[i].beforeAccess = barriers[i].afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            barriers[i].beforeSync = barriers[i].afterSync = rhi::ResourceSyncState::ComputeShading;
        }
        rhi::BarrierBatch batch{};
        batch.buffers = {barriers.data(), static_cast<uint32_t>(barriers.size())};
        commands.Barriers(batch);
    };
    const auto dispatch = [&](size_t programIndex, const auto& constants, int argumentIndex) {
        const auto& binding = data.programs[programIndex];
        commands.BindLayout(recording.ResolveLayout(binding.program));
        commands.BindPipeline(recording.Resolve(binding.program));
        if (!binding.descriptorIndices.empty()) commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(binding.descriptorIndices.size()), binding.descriptorIndices.data());
        commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
            NumMiscUintRootConstants, constants.data());
        if (argumentIndex < 0) commands.Dispatch(1, 1, 1);
        else commands.ExecuteIndirect(data.signature,
            recording.Resolve(data.indirectResources[argumentIndex]).GetHandle(), 0, {}, 0, 1);
        uavBarrier();
    };
    dispatch(0, data.constants[0], -1);
    std::array<rhi::BufferBarrier, 2> indirectBarriers{};
    for (size_t i = 0; i < indirectBarriers.size(); ++i) {
        auto& barrier = indirectBarriers[i];
        barrier.buffer = recording.Resolve(data.indirectResources[i]).GetHandle();
        barrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
        barrier.afterAccess = rhi::ResourceAccessType::IndirectArgument;
        barrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
        barrier.afterSync = rhi::ResourceSyncState::ExecuteIndirect;
    }
    rhi::BarrierBatch batch{};
    batch.buffers = {indirectBarriers.data(), static_cast<uint32_t>(indirectBarriers.size())};
    commands.Barriers(batch);
    for (uint32_t iteration = 0; iteration < 8; ++iteration) {
        auto constants = data.constants[iteration & 1u];
        constants[CLOD_STREAMING_SORT_ITERATION_INDEX] = iteration;
        dispatch(1, constants, 0);
        dispatch(2, constants, 1);
        dispatch(3, constants, -1);
        dispatch(4, constants, 1);
        dispatch(5, constants, 0);
    }
}
