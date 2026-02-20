#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rhi.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "Render/ImmediateExecution/ImmediateCommandList.h"

namespace br {

class ReadbackManager {
public:
    ReadbackManager();

    void Initialize(rhi::Timeline readbackFence);

    void RequestReadback(std::shared_ptr<PixelBuffer> texture, std::wstring outputFile, std::function<void()> callback, bool cubemap);

    std::shared_ptr<RenderPass> GetReadbackPass() const { return m_readbackPass; }

    void ProcessReadbackRequests();

    void Cleanup();

private:
    struct ReadbackInfo {
        bool cubemap = false;
        std::shared_ptr<PixelBuffer> texture;
        std::wstring outputFile;
        std::function<void()> callback;
    };

    struct ReadbackRequest {
        std::shared_ptr<Resource> readbackBuffer;
        std::vector<rhi::CopyableFootprint> layouts;
        uint64_t totalSize = 0;
        std::wstring outputFile;
        std::function<void()> callback;
        uint64_t fenceValue = 0;
    };

    class ReadbackPass : public RenderPass {
    public:
        explicit ReadbackPass(ReadbackManager& owner)
            : m_owner(owner) {
        }

        void Setup() override {
        }

        void ExecuteImmediate(ImmediateExecutionContext& context) override;

        PassReturn Execute(PassExecutionContext& context) override;

        void Cleanup() override {
        }

        void SetReadbackFence(rhi::Timeline fence) {
            m_readbackFence = fence;
        }

    private:
        ReadbackManager& m_owner;
        rhi::Timeline m_readbackFence;
        uint64_t m_fenceValue = 0;
        bool m_hasWork = false;
    };

    void ClearReadbacks();

    void SaveCubemapToDDS(
        rhi::Device& device,
        rg::imm::ImmediateCommandList& commandList,
        std::shared_ptr<PixelBuffer> cubemap,
        const std::wstring& outputFile,
        uint64_t fenceValue);

    void SaveTextureToDDS(
        rhi::Device& device,
        rg::imm::ImmediateCommandList& commandList,
        PixelBuffer* texture,
        const std::wstring& outputFile,
        uint64_t fenceValue);

    std::shared_ptr<ReadbackPass> m_readbackPass;
    rhi::Timeline m_readbackFence;
    std::mutex m_mutex;
    std::vector<ReadbackInfo> m_queuedReadbacks;
    std::vector<ReadbackRequest> m_readbackRequests;
};

} // namespace br
