#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "RenderPasses/Base/ComputePass.h"

class Buffer;

class CLodStreamingBeginFramePass : public ComputePass {
public:
    CLodStreamingBeginFramePass(
        std::shared_ptr<Buffer> loadCounter,
        std::shared_ptr<Buffer> loadRequestBits,
        std::shared_ptr<Buffer> nonResidentBits,
        std::shared_ptr<Buffer> activeGroupsBits,
        std::shared_ptr<Buffer> runtimeState,
        std::function<bool(std::vector<uint32_t>&)> tryConsumeNonResidentBitsUpload,
        std::function<void(std::vector<uint32_t>&, uint32_t&)> getActiveGroupsBitsUpload,
        std::function<uint32_t()> getBitsetWordCount,
        std::function<void()> scheduleStreamingReadbacks,
        std::function<void()> processStreamingRequests);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Update(const UpdateExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_loadCounter;
    std::shared_ptr<Buffer> m_loadRequestBits;
    std::shared_ptr<Buffer> m_nonResidentBits;
    std::shared_ptr<Buffer> m_activeGroupsBits;
    std::shared_ptr<Buffer> m_runtimeState;
    std::function<bool(std::vector<uint32_t>&)> m_tryConsumeNonResidentBitsUpload;
    std::function<void(std::vector<uint32_t>&, uint32_t&)> m_getActiveGroupsBitsUpload;
    std::function<uint32_t()> m_getBitsetWordCount;
    std::function<void()> m_scheduleStreamingReadbacks;
    std::function<void()> m_processStreamingRequests;
};
