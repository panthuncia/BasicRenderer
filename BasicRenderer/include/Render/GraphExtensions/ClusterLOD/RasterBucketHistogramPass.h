#pragma once

#include <memory>
#include <vector>

#include <rhi.h>

#include "RenderPasses/Base/TypedRenderGraphPass.h"

namespace org { class Buffer; }
using org::Buffer;

struct RasterBucketHistogramPreparedData {
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    org::PreparedProgramReference clearProgram{}, histogramProgram{};
    rhi::CommandSignatureHandle commandSignature{};
    org::PreparedResourceReference indirectArguments{}, histogramResource{};
    std::vector<unsigned int> clearDescriptorIndices, histogramDescriptorIndices;
    std::vector<uint32_t> clearConstants, histogramConstants;
    uint32_t clearGroups = 0;
    bool enabled = false;
};

class RasterBucketHistogramPass : public org::TypedRenderGraphPass<RasterBucketHistogramPass, RasterBucketHistogramPreparedData> {
public:
    RasterBucketHistogramPass(
        std::shared_ptr<Buffer> visibleClustersBuffer,
        std::shared_ptr<Buffer> visibleClustersCounterBuffer,
        std::shared_ptr<Buffer> histogramIndirectCommand,
        std::shared_ptr<Buffer> histogramBuffer,
        std::shared_ptr<Buffer> reyesOwnershipBitsetBuffer = nullptr,
        std::shared_ptr<Buffer> telemetryBuffer = nullptr,
        std::shared_ptr<Buffer> readBaseCounterBuffer = nullptr,
        bool readReverse = false,
        uint32_t visibleClustersCapacity = 0u,
        bool runWhenComputeSWRasterEnabledOnly = false);
    ~RasterBucketHistogramPass();

    void Declare(org::PassBuilder& builder);
    RasterBucketHistogramPreparedData Prepare(const org::PassPrepareContext& preparation);
    static void Record(const RasterBucketHistogramPreparedData&, org::PassRecordContext&);
    void Update(const UpdateExecutionContext& executionContext) override;

private:
    using PreparedData = RasterBucketHistogramPreparedData;
    void CreatePipelines(
        rhi::Device device,
        rhi::PipelineLayoutHandle globalRootSignature,
        PipelineState& outHistogramPipeline,
        PipelineState& outClearPipeline);

    PipelineState m_histogramPipeline;
    PipelineState m_clearPipeline;
    std::shared_ptr<rhi::CommandSignaturePtr> m_histogramCommandSignature;
    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_visibleClustersCounterBuffer;
    std::shared_ptr<Buffer> m_histogramIndirectCommand;
    std::shared_ptr<Buffer> m_histogramBuffer;
    std::shared_ptr<Buffer> m_reyesOwnershipBitsetBuffer;
    std::shared_ptr<Buffer> m_telemetryBuffer;
    std::shared_ptr<Buffer> m_readBaseCounterBuffer;
    bool m_readReverse = false;
    uint32_t m_visibleClustersCapacity = 0u;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};
