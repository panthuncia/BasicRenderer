#pragma once

#include <memory>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

class Buffer;
class PixelBuffer;

class VirtualShadowMapMarkPagesPass final : public ComputePass {
public:
    VirtualShadowMapMarkPagesPass(
        std::shared_ptr<Buffer> tileWorkBuffer,
        std::shared_ptr<Buffer> tileCountBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        std::shared_ptr<Buffer> allocationRequestsBuffer,
        std::shared_ptr<Buffer> allocationCountBuffer,
        std::shared_ptr<Buffer> markClipmapDataBuffer,
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
        std::shared_ptr<Buffer> directionalPageViewInfoBuffer,
        std::shared_ptr<Buffer> statsBuffer);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    PipelineState m_pso;
    rhi::CommandSignaturePtr m_commandSignature;
    std::shared_ptr<Buffer> m_tileWorkBuffer;
    std::shared_ptr<Buffer> m_tileCountBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    std::shared_ptr<Buffer> m_allocationRequestsBuffer;
    std::shared_ptr<Buffer> m_allocationCountBuffer;
    std::shared_ptr<Buffer> m_markClipmapDataBuffer;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_dirtyPageFlagsBuffer;
    std::shared_ptr<Buffer> m_directionalPageViewInfoBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
    uint32_t m_activeClipmapCount = 0u;
};