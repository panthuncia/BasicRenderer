#pragma once

#include <memory>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;

class RasterBucketBlockOffsetsPass : public org::TypedRenderGraphPass<RasterBucketBlockOffsetsPass, br::render::PreparedComputeDispatch> {
public:
    RasterBucketBlockOffsetsPass(
        std::shared_ptr<Buffer> offsetsBuffer,
        std::shared_ptr<Buffer> blockSumsBuffer,
        std::shared_ptr<Buffer> scannedBlockSumsBuffer,
        std::shared_ptr<Buffer> totalCountBuffer,
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
    std::shared_ptr<Buffer> m_offsetsBuffer;
    std::shared_ptr<Buffer> m_blockSumsBuffer;
    std::shared_ptr<Buffer> m_scannedBlockSumsBuffer;
    std::shared_ptr<Buffer> m_totalCountBuffer;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};
