#pragma once

#include <memory>
#include <rhi.h>

#include "RenderPasses/Base/CopyPass.h"
#include "Resources/Buffers/Buffer.h"

// Inputs identifying the GPU source buffers for the streaming readback copy.
struct CLodStreamingReadbackCopyInputs {
    std::shared_ptr<Buffer> counterSource;       // GPU load counter (1 × uint32)
    std::shared_ptr<Buffer> requestsSource;      // GPU load requests (N × CLodStreamingRequest)
    std::shared_ptr<Buffer> usedGroupsCounterSource; // GPU used-groups counter (1 × uint32)
    std::shared_ptr<Buffer> usedGroupsBufferSource;  // GPU used-groups buffer (N × uint32)
};

inline rg::Hash64 HashValue(const CLodStreamingReadbackCopyInputs& i) {
    // Structural pass re-used every frame; counter/request buffers are stable.
    std::size_t seed = 0;
    if (i.counterSource) seed ^= i.counterSource->GetGlobalResourceID();
    if (i.requestsSource) seed ^= (i.requestsSource->GetGlobalResourceID() * 0x9e3779b97f4a7c15ULL);
    if (i.usedGroupsCounterSource) seed ^= (i.usedGroupsCounterSource->GetGlobalResourceID() * 0x517cc1b727220a95ULL);
    if (i.usedGroupsBufferSource) seed ^= (i.usedGroupsBufferSource->GetGlobalResourceID() * 0x6c62272e07bb0142ULL);
    return static_cast<rg::Hash64>(seed);
}

inline bool operator==(const CLodStreamingReadbackCopyInputs& a, const CLodStreamingReadbackCopyInputs& b) {
    return a.counterSource == b.counterSource && a.requestsSource == b.requestsSource
        && a.usedGroupsCounterSource == b.usedGroupsCounterSource
        && a.usedGroupsBufferSource == b.usedGroupsBufferSource;
}

// CopyPass that copies the GPU streaming load counter + load request buffer
// to pre-allocated readback staging buffers, then returns a fence signal so
// the CPU can HostWait for completion.
class CLodStreamingReadbackCopyPass final : public CopyPass {
public:
    CLodStreamingReadbackCopyPass(
        CLodStreamingReadbackCopyInputs inputs,
        std::shared_ptr<Buffer> counterStaging,
        std::shared_ptr<Buffer> requestsStaging,
        std::shared_ptr<Buffer> usedGroupsCounterStaging,
        std::shared_ptr<Buffer> usedGroupsBufferStaging,
        rhi::Timeline fence,
        uint64_t fenceValue)
        : m_counterStaging(std::move(counterStaging))
        , m_requestsStaging(std::move(requestsStaging))
        , m_usedGroupsCounterStaging(std::move(usedGroupsCounterStaging))
        , m_usedGroupsBufferStaging(std::move(usedGroupsBufferStaging))
        , m_fence(fence)
        , m_fenceValue(fenceValue)
    {
        SetInputs(std::move(inputs));
    }

    void DeclareResourceUsages(CopyPassBuilder* builder) override {
        const auto& inputs = Inputs<CLodStreamingReadbackCopyInputs>();
        builder->WithCopySource(inputs.counterSource);
        builder->WithCopySource(inputs.requestsSource);
        builder->WithCopySource(inputs.usedGroupsCounterSource);
        builder->WithCopySource(inputs.usedGroupsBufferSource);
        builder->PreferCopyQueue();
    }

    void Setup() override {}

    void ExecuteImmediate(ImmediateExecutionContext& context) override {
        const auto& inputs = Inputs<CLodStreamingReadbackCopyInputs>();

        auto* counterResource = inputs.counterSource.get();
        auto* requestsResource = inputs.requestsSource.get();

        if (counterResource && m_counterStaging) {
            uint64_t counterBytes = 0;
            if (counterResource->TryGetBufferByteSize(counterBytes) && counterBytes > 0) {
                context.list.CopyBufferRegion(
                    m_counterStaging, 0,
                    counterResource, 0,
                    counterBytes);
            }
        }

        if (requestsResource && m_requestsStaging) {
            uint64_t requestsBytes = 0;
            if (requestsResource->TryGetBufferByteSize(requestsBytes) && requestsBytes > 0) {
                context.list.CopyBufferRegion(
                    m_requestsStaging, 0,
                    requestsResource, 0,
                    requestsBytes);
            }
        }

        auto* usedCounterResource = inputs.usedGroupsCounterSource.get();
        auto* usedBufferResource = inputs.usedGroupsBufferSource.get();

        if (usedCounterResource && m_usedGroupsCounterStaging) {
            uint64_t bytes = 0;
            if (usedCounterResource->TryGetBufferByteSize(bytes) && bytes > 0) {
                context.list.CopyBufferRegion(
                    m_usedGroupsCounterStaging, 0,
                    usedCounterResource, 0,
                    bytes);
            }
        }

        if (usedBufferResource && m_usedGroupsBufferStaging) {
            uint64_t bytes = 0;
            if (usedBufferResource->TryGetBufferByteSize(bytes) && bytes > 0) {
                context.list.CopyBufferRegion(
                    m_usedGroupsBufferStaging, 0,
                    usedBufferResource, 0,
                    bytes);
            }
        }
    }

    PassReturn Execute(PassExecutionContext& context) override {
        return { m_fence, m_fenceValue };
    }

    void Cleanup() override {}

private:
    std::shared_ptr<Buffer> m_counterStaging;
    std::shared_ptr<Buffer> m_requestsStaging;
    std::shared_ptr<Buffer> m_usedGroupsCounterStaging;
    std::shared_ptr<Buffer> m_usedGroupsBufferStaging;
    rhi::Timeline m_fence;
    uint64_t m_fenceValue;
};
