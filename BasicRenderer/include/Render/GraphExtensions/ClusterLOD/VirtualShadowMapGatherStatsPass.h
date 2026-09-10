#pragma once

#include <memory>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct VirtualShadowMapGatherStatsBindings {
    org::ResourceBindingToken pageTable, allocationCount, allocationArgs, header, pageMetadata, clipmapInfo, stats;
    bool capturePreAllocateState = false;
};

class VirtualShadowMapGatherStatsPass final : public org::TypedRenderGraphPass<VirtualShadowMapGatherStatsPass,
    br::render::PreparedComputeDispatch, VirtualShadowMapGatherStatsBindings> {
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

    VirtualShadowMapGatherStatsBindings Declare(org::PassBuilder& builder);
    void Initialize();
    br::render::PreparedComputeDispatch Prepare(const VirtualShadowMapGatherStatsBindings&,
        const org::PassPrepareContext& preparation) const;
    static void Record(const VirtualShadowMapGatherStatsBindings&,
        const br::render::PreparedComputeDispatch&, org::PassRecordContext&);
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
