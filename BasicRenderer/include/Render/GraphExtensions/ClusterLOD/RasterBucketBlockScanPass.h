#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

class RasterBucketBlockScanPass : public org::TypedRenderGraphPass<RasterBucketBlockScanPass, br::render::PreparedComputeDispatch> {
public:
    RasterBucketBlockScanPass(
        std::shared_ptr<Buffer> histogramBuffer,
        std::shared_ptr<Buffer> offsetsBuffer,
        std::shared_ptr<Buffer> blockSumsBuffer,
        bool runWhenComputeSWRasterEnabledOnly = false);

    void Declare(org::PassBuilder& builder);
    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation);
    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& context) {
        br::render::RecordPreparedComputeDispatch(data, context);
    }
    void Update(const UpdateExecutionContext& executionContext) override;

private:
    PipelineState m_pso;
    uint32_t m_blockSize = 1024;
    std::shared_ptr<Buffer> m_histogramBuffer;
    std::shared_ptr<Buffer> m_offsetsBuffer;
    std::shared_ptr<Buffer> m_blockSumsBuffer;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};
