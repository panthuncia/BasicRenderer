#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

struct AVBOITDepthWarpBindings { org::ResourceBindingToken config, histogram, lut; };
class AVBOITDepthWarpPass final : public org::TypedRenderGraphPass<AVBOITDepthWarpPass, br::render::PreparedComputeDispatch, AVBOITDepthWarpBindings> {
public:
    AVBOITDepthWarpPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<Buffer> occupancyHistogramBuffer,
        std::shared_ptr<Buffer> depthWarpLUTBuffer);

    AVBOITDepthWarpBindings Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const AVBOITDepthWarpBindings&, const org::PassPrepareContext&) const;
    static void Record(const AVBOITDepthWarpBindings&, const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<Buffer> m_occupancyHistogramBuffer;
    std::shared_ptr<Buffer> m_depthWarpLUTBuffer;
    PipelineState m_pso;
};
