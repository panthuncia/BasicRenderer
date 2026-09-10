#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

struct AVBOITAdaptiveFitBindings {
    org::ResourceBindingToken config, state;
};

class AVBOITAdaptiveFitPass final : public org::TypedRenderGraphPass<AVBOITAdaptiveFitPass,
    br::render::PreparedComputeDispatch, AVBOITAdaptiveFitBindings> {
public:
    AVBOITAdaptiveFitPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<Buffer> fitStateBuffer);

    AVBOITAdaptiveFitBindings Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const AVBOITAdaptiveFitBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const AVBOITAdaptiveFitBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<Buffer> m_fitStateBuffer;
    PipelineState m_pso;
};
