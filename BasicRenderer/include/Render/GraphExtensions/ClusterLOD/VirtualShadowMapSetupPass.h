#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"

class Buffer;
class PixelBuffer;

class VirtualShadowMapSetupPass final : public ComputePass {
public:
    VirtualShadowMapSetupPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> allocationCountBuffer,
        std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
        std::shared_ptr<Buffer> clipmapInfoBuffer,
        std::shared_ptr<Buffer> runtimeStateBuffer,
        bool forceResetResources);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_allocationCountBuffer;
    std::shared_ptr<Buffer> m_dirtyPageFlagsBuffer;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
    std::shared_ptr<Buffer> m_runtimeStateBuffer;
    bool m_forceResetResources = false;
    bool m_resetResources = false;
};