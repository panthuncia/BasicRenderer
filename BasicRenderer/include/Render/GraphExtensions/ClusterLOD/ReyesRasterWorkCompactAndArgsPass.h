#pragma once

#include <memory>

#include <rhi.h>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

struct ReyesCompactFrameData {
    br::render::PreparedComputeDispatch clear, finalize;
    br::render::PreparedComputeIndirect compact, pack;
    org::PreparedResourceReference cursorBarrier, compactedBarrier, packedBarrier;
};

class ReyesRasterWorkCompactAndArgsPass final : public org::TypedRenderGraphPass<ReyesRasterWorkCompactAndArgsPass, ReyesCompactFrameData> {
public:
    ReyesRasterWorkCompactAndArgsPass(
        std::shared_ptr<Buffer> rasterWorkBuffer,
        std::shared_ptr<Buffer> rasterWorkCounterBuffer,
        std::shared_ptr<Buffer> indirectCommand,
        std::shared_ptr<Buffer> histogramBuffer,
        std::shared_ptr<Buffer> offsetsBuffer,
        std::shared_ptr<Buffer> writeCursorBuffer,
        std::shared_ptr<Buffer> compactedRasterWorkIndicesBuffer,
        std::shared_ptr<Buffer> packedRasterWorkGroupsBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer);

    void Declare(org::PassBuilder& builder);
    ReyesCompactFrameData Prepare(const org::PassPrepareContext& preparation);
    static void Record(const ReyesCompactFrameData& data, org::PassRecordContext& recording);
    void Update(const UpdateExecutionContext& executionContext) override;

private:
    PipelineState m_pso;
    PipelineState m_packPipeline;
    PipelineState m_finalizePackPipeline;
    PipelineState m_clearPipeline;
    std::shared_ptr<rhi::CommandSignaturePtr> m_compactionCommandSignature;

    std::shared_ptr<Buffer> m_rasterWorkBuffer;
    std::shared_ptr<Buffer> m_rasterWorkCounterBuffer;
    std::shared_ptr<Buffer> m_indirectCommand;
    std::shared_ptr<Buffer> m_histogramBuffer;
    std::shared_ptr<Buffer> m_offsetsBuffer;
    std::shared_ptr<Buffer> m_writeCursorBuffer;
    std::shared_ptr<Buffer> m_compactedRasterWorkIndicesBuffer;
    std::shared_ptr<Buffer> m_packedRasterWorkGroupsBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
};
