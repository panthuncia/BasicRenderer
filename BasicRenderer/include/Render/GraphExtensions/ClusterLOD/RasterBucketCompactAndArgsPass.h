#pragma once

#include <memory>

#include <rhi.h>

#include "RenderPasses/Base/ComputePass.h"

class Buffer;

class RasterBucketCompactAndArgsPass : public ComputePass {
public:
    RasterBucketCompactAndArgsPass(
        std::shared_ptr<Buffer> visibleClustersBuffer,
        std::shared_ptr<Buffer> visibleClustersCounterBuffer,
        std::shared_ptr<Buffer> compactedBaseCounterBuffer,
        std::shared_ptr<Buffer> indirectCommand,
        std::shared_ptr<Buffer> histogramBuffer,
        std::shared_ptr<Buffer> offsetsBuffer,
        std::shared_ptr<Buffer> writeCursorBuffer,
        std::shared_ptr<Buffer> compactedClustersBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        uint64_t maxVisibleClusters,
        bool appendToExisting);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Update(const UpdateExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    PipelineState m_pso;
    rhi::CommandSignaturePtr m_compactionCommandSignature;

    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_compactedBaseCounterBuffer;
    std::shared_ptr<Buffer> m_indirectCommand;
    std::shared_ptr<Buffer> m_histogramBuffer;
    std::shared_ptr<Buffer> m_offsetsBuffer;
    std::shared_ptr<Buffer> m_writeCursorBuffer;
    std::shared_ptr<Buffer> m_compactedClustersBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;

    uint64_t m_maxVisibleClusters = 0;
    bool m_appendToExisting = false;
};
