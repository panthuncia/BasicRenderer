#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

class Buffer;
class PixelBuffer;

class VirtualShadowMapNonRasterableHierarchyPass final : public ComputePass {
public:
    VirtualShadowMapNonRasterableHierarchyPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<PixelBuffer> nonRasterableHierarchyTexture,
        std::shared_ptr<Buffer> clipmapInfoBuffer);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<PixelBuffer> m_nonRasterableHierarchyTexture;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
};