#pragma once

#include <memory>

#include "RenderPasses/Base/ComputePass.h"

class Buffer;
class PixelBuffer;

class FixedSliceScalarVBOITOccupancyHistogramPass final : public ComputePass {
public:
    FixedSliceScalarVBOITOccupancyHistogramPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
        std::shared_ptr<Buffer> occupancyHistogramBuffer);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_occupancySliceMaskTexture;
    std::shared_ptr<Buffer> m_occupancyHistogramBuffer;
    PipelineState m_pso;
};