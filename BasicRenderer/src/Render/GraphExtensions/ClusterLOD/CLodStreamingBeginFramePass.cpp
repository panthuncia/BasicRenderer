#include "Render/GraphExtensions/ClusterLOD/CLodStreamingBeginFramePass.h"

#include <algorithm>

#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "BuiltinResources.h"
#include "ShaderBuffers.h"

CLodStreamingBeginFramePass::CLodStreamingBeginFramePass(
    std::shared_ptr<Buffer> loadCounter,
    std::shared_ptr<Buffer> loadRequestBits,
    std::shared_ptr<Buffer> nonResidentBits,
    std::shared_ptr<Buffer> activeGroupsBits,
    std::shared_ptr<Buffer> runtimeState,
    std::function<bool(std::vector<uint32_t>&)> tryConsumeNonResidentBitsUpload,
    std::function<void(std::vector<uint32_t>&, uint32_t&)> getActiveGroupsBitsUpload,
    std::function<uint32_t()> getBitsetWordCount,
    std::function<void()> scheduleStreamingReadbacks,
    std::function<void()> processStreamingRequests)
    : m_loadCounter(std::move(loadCounter))
    , m_loadRequestBits(std::move(loadRequestBits))
    , m_nonResidentBits(std::move(nonResidentBits))
    , m_activeGroupsBits(std::move(activeGroupsBits))
    , m_runtimeState(std::move(runtimeState))
    , m_tryConsumeNonResidentBitsUpload(std::move(tryConsumeNonResidentBitsUpload))
    , m_getActiveGroupsBitsUpload(std::move(getActiveGroupsBitsUpload))
    , m_getBitsetWordCount(std::move(getBitsetWordCount))
    , m_scheduleStreamingReadbacks(std::move(scheduleStreamingReadbacks))
    , m_processStreamingRequests(std::move(processStreamingRequests)) {
}

void CLodStreamingBeginFramePass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithUnorderedAccess(m_loadCounter, m_nonResidentBits, m_activeGroupsBits, m_runtimeState)
        .WithUnorderedAccess(Builtin::CLod::StreamingLoadRequestBits);
}

void CLodStreamingBeginFramePass::Setup() {}

PassReturn CLodStreamingBeginFramePass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;
    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    return {};
}

void CLodStreamingBeginFramePass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData ? executionContext.hostData->Get<UpdateContext>() : nullptr;
    if (!updateContext) {
        return;
    }

    if (m_scheduleStreamingReadbacks) {
        m_scheduleStreamingReadbacks();
    }
    if (m_processStreamingRequests) {
        m_processStreamingRequests();
    }

    const uint32_t zero = 0u;
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_loadCounter), 0);

    const uint32_t bitsetWordCount = std::max(m_getBitsetWordCount ? m_getBitsetWordCount() : 1u, 1u);
    std::vector<uint32_t> zeroBits(bitsetWordCount, 0u);
    BUFFER_UPLOAD(
        zeroBits.data(),
        static_cast<uint32_t>(zeroBits.size() * sizeof(uint32_t)),
        rg::runtime::UploadTarget::FromShared(m_loadRequestBits),
        0);

    std::vector<uint32_t> nonResidentBitsUpload;
    if (m_tryConsumeNonResidentBitsUpload && m_tryConsumeNonResidentBitsUpload(nonResidentBitsUpload)) {
        rg::runtime::QueueStreamingUploadDispatch(
            nonResidentBitsUpload.data(),
            nonResidentBitsUpload.size() * sizeof(uint32_t),
            m_nonResidentBits,
            0);
    }

    std::vector<uint32_t> activeGroupsBitsUpload;
    uint32_t activeGroupScanCount = 0u;
    if (m_getActiveGroupsBitsUpload) {
        m_getActiveGroupsBitsUpload(activeGroupsBitsUpload, activeGroupScanCount);
    }
    if (!activeGroupsBitsUpload.empty()) {
        BUFFER_UPLOAD(
            activeGroupsBitsUpload.data(),
            static_cast<uint32_t>(activeGroupsBitsUpload.size() * sizeof(uint32_t)),
            rg::runtime::UploadTarget::FromShared(m_activeGroupsBits),
            0);
    }

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

void CLodStreamingBeginFramePass::Cleanup() {}
