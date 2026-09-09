#pragma once

#include <memory>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

struct ReyesHistogramFrameData {
    br::render::PreparedComputeDispatch clear;
    br::render::PreparedComputeIndirect histogram;
    org::PreparedResourceReference histogramBarrier;
};

class ReyesRasterWorkHistogramPass final : public org::TypedRenderGraphPass<ReyesRasterWorkHistogramPass, ReyesHistogramFrameData> {
public:
    ReyesRasterWorkHistogramPass(
        std::shared_ptr<Buffer> rasterWorkBuffer,
        std::shared_ptr<Buffer> rasterWorkCounterBuffer,
        std::shared_ptr<Buffer> histogramIndirectCommand,
        std::shared_ptr<Buffer> histogramBuffer);

    void Declare(org::PassBuilder& builder);
    ReyesHistogramFrameData Prepare(const org::PassPrepareContext& preparation);
    static void Record(const ReyesHistogramFrameData& data, org::PassRecordContext& recording);
    void Update(const UpdateExecutionContext& executionContext) override;

private:
    void CreatePipelines(
        rhi::Device device,
        rhi::PipelineLayoutHandle globalRootSignature,
        PipelineState& outHistogramPipeline,
        PipelineState& outClearPipeline);

    PipelineState m_histogramPipeline;
    PipelineState m_clearPipeline;
    std::shared_ptr<rhi::CommandSignaturePtr> m_histogramCommandSignature;
    std::shared_ptr<Buffer> m_rasterWorkBuffer;
    std::shared_ptr<Buffer> m_rasterWorkCounterBuffer;
    std::shared_ptr<Buffer> m_histogramIndirectCommand;
    std::shared_ptr<Buffer> m_histogramBuffer;
};
