#include "Render/Runtime/IReadbackService.h"

#include "Managers/Singletons/ReadbackManager.h"

namespace rg::runtime {

namespace {
class DefaultReadbackService final : public IReadbackService {
public:
    void Initialize(rhi::Timeline readbackFence) override {
        ReadbackManager::GetInstance().Initialize(readbackFence);
    }

    void RequestReadbackCapture(const std::string& passName, Resource* resource, const RangeSpec& range, ReadbackCaptureCallback callback) override {
        ReadbackManager::GetInstance().RequestReadbackCapture(passName, resource, range, std::move(callback));
    }

    std::vector<rg::runtime::ReadbackCaptureInfo> ConsumeCaptureRequests() override {
        auto captures = ReadbackManager::GetInstance().ConsumeCaptureRequests();
        std::vector<rg::runtime::ReadbackCaptureInfo> out;
        out.reserve(captures.size());
        for (auto& capture : captures) {
            out.push_back({
                .passName = std::move(capture.passName),
                .resource = std::move(capture.resource),
                .range = capture.range,
                .callback = std::move(capture.callback)
                });
        }
        return out;
    }

    rg::runtime::ReadbackCaptureToken EnqueueCapture(ReadbackCaptureRequest&& request) override {
        auto token = ReadbackManager::GetInstance().EnqueueCapture(std::move(request));
        return { token.id };
    }

    void FinalizeCapture(rg::runtime::ReadbackCaptureToken token, uint64_t fenceValue) override {
        ReadbackManager::GetInstance().FinalizeCapture({ token.id }, fenceValue);
    }

    uint64_t GetNextReadbackFenceValue() override {
        return ReadbackManager::GetInstance().GetNextReadbackFenceValue();
    }

    rhi::Timeline GetReadbackFence() const override {
        return ReadbackManager::GetInstance().GetReadbackFence();
    }

    void ProcessReadbackRequests() override {
        ReadbackManager::GetInstance().ProcessReadbackRequests();
    }

    void Cleanup() override {
        ReadbackManager::GetInstance().Cleanup();
    }
};
}

std::shared_ptr<IReadbackService> CreateDefaultReadbackService() {
    return std::make_shared<DefaultReadbackService>();
}

}
