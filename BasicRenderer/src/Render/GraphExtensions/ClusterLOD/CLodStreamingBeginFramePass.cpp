#include "Render/GraphExtensions/ClusterLOD/CLodStreamingBeginFramePass.h"

#include <algorithm>

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/PassBuilders.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Managers/UploadInstance.h"
#include "BuiltinResources.h"
#include "ShaderBuffers.h"
#include "../shaders/PerPassRootConstants/clodClearUintBufferRootConstants.h"

CLodStreamingBeginFramePass::CLodStreamingBeginFramePass(
    std::function<UploadInstance*()> getUploadInstance,
    std::shared_ptr<Buffer> loadCounter,
    std::shared_ptr<Buffer> loadRequestKeys,
    std::shared_ptr<Buffer> usedGroupsCounter,
    std::shared_ptr<Buffer> nonResidentBits,
    std::shared_ptr<Buffer> activeGroupsBits,
    std::shared_ptr<Buffer> runtimeState,
    std::function<bool(std::vector<uint32_t>&, uint32_t&)> tryConsumeNonResidentBitsUpload,
    std::function<bool(std::vector<uint32_t>&, uint32_t&)> getActiveGroupsBitsUpload,
    std::function<void()> scheduleStreamingReadbacks,
    std::function<void()> processStreamingRequests)
    : m_loadCounter(std::move(loadCounter))
    , m_loadRequestKeys(std::move(loadRequestKeys))
    , m_usedGroupsCounter(std::move(usedGroupsCounter))
    , m_nonResidentBits(std::move(nonResidentBits))
    , m_activeGroupsBits(std::move(activeGroupsBits))
    , m_runtimeState(std::move(runtimeState))
    , m_tryConsumeNonResidentBitsUpload(std::move(tryConsumeNonResidentBitsUpload))
    , m_getActiveGroupsBitsUpload(std::move(getActiveGroupsBitsUpload))
    , m_scheduleStreamingReadbacks(std::move(scheduleStreamingReadbacks))
    , m_processStreamingRequests(std::move(processStreamingRequests))
    , m_getUploadInstance(std::move(getUploadInstance))
{
    m_clearUintPipeline = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"ClearUintStructuredBufferCSMain",
        {},
        "CLodStreamingBeginFrameClearUint");
}

void CLodStreamingBeginFramePass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithUnorderedAccess(m_loadCounter, m_loadRequestKeys, m_usedGroupsCounter, m_nonResidentBits, m_activeGroupsBits, m_runtimeState);
}

void CLodStreamingBeginFramePass::Setup() {}

PassReturn CLodStreamingBeginFramePass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;
    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

    if (m_loadRequestKeys) {
        ZoneScopedN("CLodStreamingBeginFramePass::ClearRequestKeys");
        BindResourceDescriptorIndices(commandList, m_clearUintPipeline.GetResourceDescriptorSlots());
        commandList.BindPipeline(m_clearUintPipeline.GetAPIPipelineState().GetHandle());

        uint32_t clearRootConstants[NumMiscUintRootConstants] = {};
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_DESCRIPTOR_INDEX] = m_loadRequestKeys->GetUAVShaderVisibleInfo(0).slot.index;
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_VALUE] = 0xffffffffu;
        clearRootConstants[CLOD_CLEAR_UINT_BUFFER_COUNT] = CLodStreamingRequestCapacity;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            clearRootConstants);
        commandList.Dispatch((CLodStreamingRequestCapacity + 63u) / 64u, 1u, 1u);
    }
    return {};
}

void CLodStreamingBeginFramePass::Update(const UpdateExecutionContext& executionContext) {
    ZoneScopedN("CLodStreamingBeginFramePass::Update");

    auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
    if (!updateContext) {
        return;
    }

    // Retire upload-heap pages from completed frames.
    UploadInstance* uploadInstance = m_getUploadInstance ? m_getUploadInstance() : nullptr;
    if (uploadInstance) {
        ZoneScopedN("CLodStreamingBeginFramePass::ProcessDeferredReleases");
        uploadInstance->ProcessDeferredReleases(static_cast<uint8_t>(executionContext.frameIndex));
    }

    if (m_scheduleStreamingReadbacks) {
        ZoneScopedN("CLodStreamingBeginFramePass::PollReadbacks");
        m_scheduleStreamingReadbacks();
    }
    if (m_processStreamingRequests) {
        ZoneScopedN("CLodStreamingBeginFramePass::ProcessStreamingRequests");
        m_processStreamingRequests();
    }

    // Counters and metadata go through BUFFER_UPLOAD (graphics-queue upload
    // manager) so they are visible to culling passes this frame.
    const uint32_t zero = 0u;
    {
        ZoneScopedN("CLodStreamingBeginFramePass::UploadCounters");
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_loadCounter), 0);
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_usedGroupsCounter), 0);
    }

    uint32_t activeGroupScanCount = 0u;
    {
        ZoneScopedN("CLodStreamingBeginFramePass::UploadActiveGroupsBits");
        const bool activeGroupsBitsUploadPending = m_getActiveGroupsBitsUpload
            && m_getActiveGroupsBitsUpload(m_activeGroupsBitsUploadScratch, activeGroupScanCount);
        if (activeGroupsBitsUploadPending && !m_activeGroupsBitsUploadScratch.empty()) {
            BUFFER_UPLOAD(
                m_activeGroupsBitsUploadScratch.data(),
                static_cast<uint32_t>(m_activeGroupsBitsUploadScratch.size() * sizeof(uint32_t)),
                rg::runtime::UploadTarget::FromShared(m_activeGroupsBits),
                0);
        }
    }

    {
        ZoneScopedN("CLodStreamingBeginFramePass::UploadRuntimeState");
        CLodStreamingRuntimeState state{};
        state.activeGroupScanCount = activeGroupScanCount;
        state.unloadAfterFrames = 0u;
        state.activeGroupsBitsetWordCount = CLodBitsetWordCount(activeGroupScanCount);
        BUFFER_UPLOAD(
            &state,
            sizeof(CLodStreamingRuntimeState),
            rg::runtime::UploadTarget::FromShared(m_runtimeState),
            0);
    }

    // nonResidentBits stays on UploadInstance so it arrives in the same copy
    // batch as slab page data — residency is never advertised before data lands.
    if (!uploadInstance) return;

    uint32_t nonResidentFirstWord = 0u;
    {
        ZoneScopedN("CLodStreamingBeginFramePass::UploadNonResidentBits");
        if (m_tryConsumeNonResidentBitsUpload
            && m_tryConsumeNonResidentBitsUpload(m_nonResidentBitsUploadScratch, nonResidentFirstWord)
            && !m_nonResidentBitsUploadScratch.empty()) {
            uploadInstance->UploadData(
                m_nonResidentBitsUploadScratch.data(),
                static_cast<uint32_t>(m_nonResidentBitsUploadScratch.size() * sizeof(uint32_t)),
                rg::runtime::UploadTarget::FromShared(m_nonResidentBits),
                nonResidentFirstWord * sizeof(uint32_t));
        }
    }
}

void CLodStreamingBeginFramePass::Cleanup() {}
