#pragma once

#include <memory>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

class Buffer;
class PixelBuffer;

class VirtualShadowMapClearDirtyBitsPass final : public ComputePass {
public:
    VirtualShadowMapClearDirtyBitsPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> allocationRequestsBuffer,
        std::shared_ptr<Buffer> allocationCountBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_allocationRequestsBuffer;
    std::shared_ptr<Buffer> m_allocationCountBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    rhi::CommandSignaturePtr m_commandSignature;
};