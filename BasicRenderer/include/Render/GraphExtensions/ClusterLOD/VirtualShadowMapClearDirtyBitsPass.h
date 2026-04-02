#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

class PixelBuffer;

class VirtualShadowMapClearDirtyBitsPass final : public ComputePass {
public:
    explicit VirtualShadowMapClearDirtyBitsPass(std::shared_ptr<PixelBuffer> pageTableTexture);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
};