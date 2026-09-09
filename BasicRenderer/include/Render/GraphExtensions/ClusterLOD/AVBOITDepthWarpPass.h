#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

class AVBOITDepthWarpPass final : public org::TypedRenderGraphPass<AVBOITDepthWarpPass, br::render::PreparedComputeDispatch> {
public:
    AVBOITDepthWarpPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<Buffer> occupancyHistogramBuffer,
        std::shared_ptr<Buffer> depthWarpLUTBuffer);

    void Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<Buffer> m_occupancyHistogramBuffer;
    std::shared_ptr<Buffer> m_depthWarpLUTBuffer;
    PipelineState m_pso;
};