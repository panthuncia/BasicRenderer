#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct AVBOITOccupancyRemapBindings { org::ResourceBindingToken config, lut, occupancy; };
class AVBOITOccupancyRemapPass final : public org::TypedRenderGraphPass<AVBOITOccupancyRemapPass, br::render::PreparedComputeDispatch, AVBOITOccupancyRemapBindings> {
public:
    AVBOITOccupancyRemapPass(
        std::shared_ptr<Buffer> configBuffer,
        std::shared_ptr<PixelBuffer> occupancyTexture,
        std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
        std::shared_ptr<Buffer> depthWarpLUTBuffer);

    AVBOITOccupancyRemapBindings Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const AVBOITOccupancyRemapBindings&, const org::PassPrepareContext&) const;
    static void Record(const AVBOITOccupancyRemapBindings&, const br::render::PreparedComputeDispatch&, org::PassRecordContext&);

private:
    std::shared_ptr<Buffer> m_configBuffer;
    std::shared_ptr<PixelBuffer> m_occupancyTexture;
    std::shared_ptr<PixelBuffer> m_occupancySliceMaskTexture;
    std::shared_ptr<Buffer> m_depthWarpLUTBuffer;
    PipelineState m_pso;
};
