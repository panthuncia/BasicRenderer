#pragma once

#include <memory>

#include "Managers/Singletons/PSOManager.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "ShaderBuffers.h"
#include <array>
#include <vector>

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct AVBOITEarlyDepthFrameData {
    bool enabled = false;
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    org::PreparedProgramReference program;
    org::PreparedDescriptorReference depth;
    org::PreparedResourceReference arguments, count;
    rhi::CommandSignatureHandle signature{};
    rhi::ClearValue clear{};
    uint32_t width = 0, height = 0, maximumCount = 0;
    std::vector<unsigned int> descriptorIndices;
    std::array<unsigned int, NumMiscUintRootConstants> constants{};
};

class AVBOITEarlyDepthPass final : public org::TypedRenderGraphPass<AVBOITEarlyDepthPass, AVBOITEarlyDepthFrameData> {
public:
    AVBOITEarlyDepthPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<Buffer> tileCommandsBuffer,
        std::shared_ptr<Buffer> tileCountBuffer,
        std::shared_ptr<PixelBuffer> earlyDepthTexture);

    void Declare(org::PassBuilder& builder);
    AVBOITEarlyDepthFrameData Prepare(const org::PassPrepareContext& preparation);
    static void Record(const AVBOITEarlyDepthFrameData&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<Buffer> m_tileCommandsBuffer;
    std::shared_ptr<Buffer> m_tileCountBuffer;
    std::shared_ptr<PixelBuffer> m_earlyDepthTexture;
    PipelineState m_pso;
    std::shared_ptr<rhi::CommandSignaturePtr> m_commandSignature;
};