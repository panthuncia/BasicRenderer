#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class AVBOITEarlyDepthBuildPass final : public org::TypedRenderGraphPass<AVBOITEarlyDepthBuildPass, br::render::PreparedComputeDispatch> {
public:
    AVBOITEarlyDepthBuildPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture,
        std::shared_ptr<Buffer> tileCommandsBuffer,
        std::shared_ptr<Buffer> tileCountBuffer);

    void Declare(org::PassBuilder& builder);
    void Update(const UpdateExecutionContext& executionContext) override;
    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_zeroTransmittanceSliceTexture;
    std::shared_ptr<Buffer> m_tileCommandsBuffer;
    std::shared_ptr<Buffer> m_tileCountBuffer;
    PipelineState m_pso;
};