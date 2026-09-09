#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "ShaderBuffers.h"
#include "RenderPasses/PreparedResourceClears.h"
#include <array>
#include <vector>

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

using AVBOITSetupFrameData = br::render::PreparedResourceClears;

struct AVBOITSetupBindings {
    std::vector<org::ResourceBindingToken> clears;
    std::vector<org::ResourceBindingToken> targets;
};

class AVBOITSetupPass final : public org::TypedRenderGraphPass<AVBOITSetupPass,
    AVBOITSetupFrameData, AVBOITSetupBindings> {
public:
    AVBOITSetupPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<Buffer> fitStateBuffer,
        std::shared_ptr<Buffer> depthWarpLUTBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> coverageTexture,
        std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
        std::shared_ptr<PixelBuffer> scalarExtinctionTexture,
        std::shared_ptr<PixelBuffer> chromaticExtinctionTexture,
        std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
        std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture,
        std::shared_ptr<PixelBuffer> accumulationTexture,
        std::shared_ptr<PixelBuffer> normalizationTexture,
        std::shared_ptr<PixelBuffer> shadingExtinctionTexture);

    AVBOITSetupBindings Declare(org::PassBuilder& builder);
    void Update(const UpdateExecutionContext& executionContext) override;
    AVBOITSetupFrameData Prepare(const AVBOITSetupBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const AVBOITSetupBindings&, const AVBOITSetupFrameData&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<Buffer> m_fitStateBuffer;
    std::shared_ptr<Buffer> m_depthWarpLUTBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_coverageTexture;
    std::shared_ptr<PixelBuffer> m_occupancySliceMaskTexture;
    std::shared_ptr<PixelBuffer> m_scalarExtinctionTexture;
    std::shared_ptr<PixelBuffer> m_chromaticExtinctionTexture;
    std::shared_ptr<PixelBuffer> m_integratedTransmittanceTexture;
    std::shared_ptr<PixelBuffer> m_zeroTransmittanceSliceTexture;
    std::shared_ptr<PixelBuffer> m_accumulationTexture;
    std::shared_ptr<PixelBuffer> m_normalizationTexture;
    std::shared_ptr<PixelBuffer> m_shadingExtinctionTexture;
    bool m_fitStateInitialized = false;
};
