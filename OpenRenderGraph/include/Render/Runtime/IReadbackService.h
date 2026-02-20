#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rhi.h>

#include "Resources/ReadbackRequest.h"

class RenderPass;
class Resource;

namespace rg::runtime {

struct ReadbackCaptureInfo {
    std::string passName;
    std::weak_ptr<Resource> resource;
    RangeSpec range{};
    ReadbackCaptureCallback callback;
};

struct ReadbackCaptureToken {
    uint64_t id = 0;
};

class IReadbackService {
public:
    virtual ~IReadbackService() = default;

    virtual void Initialize(rhi::Timeline readbackFence) = 0;
    virtual void RequestReadbackCapture(const std::string& passName, Resource* resource, const RangeSpec& range, ReadbackCaptureCallback callback) = 0;
    virtual std::vector<ReadbackCaptureInfo> ConsumeCaptureRequests() = 0;
    virtual ReadbackCaptureToken EnqueueCapture(ReadbackCaptureRequest&& request) = 0;
    virtual void FinalizeCapture(ReadbackCaptureToken token, uint64_t fenceValue) = 0;
    virtual uint64_t GetNextReadbackFenceValue() = 0;
    virtual rhi::Timeline GetReadbackFence() const = 0;
    virtual void ProcessReadbackRequests() = 0;
    virtual void Cleanup() = 0;
};

std::shared_ptr<IReadbackService> CreateDefaultReadbackService();

}
