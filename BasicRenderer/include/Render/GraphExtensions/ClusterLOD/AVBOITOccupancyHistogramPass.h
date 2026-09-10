#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct AVBOITOccupancyHistogramBindings {
    org::ResourceBindingToken config, occupancy, sliceMask, histogram;
};

class AVBOITOccupancyHistogramPass final : public org::TypedRenderGraphPass<AVBOITOccupancyHistogramPass,
    br::render::PreparedComputeDispatch, AVBOITOccupancyHistogramBindings> {
public:
    AVBOITOccupancyHistogramPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
        std::shared_ptr<Buffer> occupancyHistogramBuffer);

    AVBOITOccupancyHistogramBindings Declare(org::PassBuilder& builder);
    void Update(const UpdateExecutionContext& executionContext) override;
    br::render::PreparedComputeDispatch Prepare(const AVBOITOccupancyHistogramBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const AVBOITOccupancyHistogramBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_occupancySliceMaskTexture;
    std::shared_ptr<Buffer> m_occupancyHistogramBuffer;
    PipelineState m_pso;
};
