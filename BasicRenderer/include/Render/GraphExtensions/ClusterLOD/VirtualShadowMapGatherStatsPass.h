#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapGatherStatsPass final : public org::TypedRenderGraphPass<VirtualShadowMapGatherStatsPass, br::render::PreparedComputeDispatch> {
public:
    VirtualShadowMapGatherStatsPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> allocationCountBuffer,
        std::shared_ptr<Buffer> allocationIndirectArgsBuffer,
        std::shared_ptr<Buffer> pageListHeaderBuffer,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> clipmapInfoBuffer,
        std::shared_ptr<Buffer> statsBuffer,
        bool capturePreAllocateState);

    void Declare(org::PassBuilder& builder);
    void Initialize();
    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
    void ShutdownPass();

private:
    PipelineState m_pso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_allocationCountBuffer;
    std::shared_ptr<Buffer> m_allocationIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_pageListHeaderBuffer;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
    bool m_capturePreAllocateState = false;
};
