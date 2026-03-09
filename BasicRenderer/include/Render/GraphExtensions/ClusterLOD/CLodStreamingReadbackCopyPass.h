#pragma once

#include <memory>
#include <rhi.h>

#include "RenderPasses/Base/CopyPass.h"
#include "Resources/Buffers/Buffer.h"

// Inputs identifying the GPU source buffers for the streaming readback copy.
struct CLodStreamingReadbackCopyInputs {
    std::shared_ptr<Buffer> counterSource;  // GPU load counter (1 × uint32)
    std::shared_ptr<Buffer> requestsSource; // GPU load requests (N × CLodStreamingRequest)
};

inline rg::Hash64 HashValue(const CLodStreamingReadbackCopyInputs& i) {
    // Structural pass re-used every frame; counter/request buffers are stable.
    std::size_t seed = 0;
    if (i.counterSource) seed ^= i.counterSource->GetGlobalResourceID();
    if (i.requestsSource) seed ^= (i.requestsSource->GetGlobalResourceID() * 0x9e3779b97f4a7c15ULL);
    return static_cast<rg::Hash64>(seed);
}

inline bool operator==(const CLodStreamingReadbackCopyInputs& a, const CLodStreamingReadbackCopyInputs& b) {
    return a.counterSource == b.counterSource && a.requestsSource == b.requestsSource;
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
        rhi::Timeline fence,
        uint64_t fenceValue)
        : m_counterStaging(std::move(counterStaging))
        , m_requestsStaging(std::move(requestsStaging))
        , m_fence(fence)
        , m_fenceValue(fenceValue)
    {
        SetInputs(std::move(inputs));
    }

    void DeclareResourceUsages(CopyPassBuilder* builder) override {
        const auto& inputs = Inputs<CLodStreamingReadbackCopyInputs>();
        builder->WithCopySource(inputs.counterSource);
        builder->WithCopySource(inputs.requestsSource);
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
    }

    PassReturn Execute(PassExecutionContext& context) override {
        return { m_fence, m_fenceValue };
    }

    void Cleanup() override {}

private:
    std::shared_ptr<Buffer> m_counterStaging;
    std::shared_ptr<Buffer> m_requestsStaging;
    rhi::Timeline m_fence;
    uint64_t m_fenceValue;
};
