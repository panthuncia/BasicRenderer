#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockOffsetsPass.h"

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "../shaders/PerPassRootConstants/clodRootConstants.h"

RasterBucketBlockOffsetsPass::RasterBucketBlockOffsetsPass(
    std::shared_ptr<Buffer> offsetsBuffer,
    std::shared_ptr<Buffer> blockSumsBuffer,
    std::shared_ptr<Buffer> scannedBlockSumsBuffer,
    std::shared_ptr<Buffer> totalCountBuffer)
    : m_offsetsBuffer(std::move(offsetsBuffer))
    , m_blockSumsBuffer(std::move(blockSumsBuffer))
    , m_scannedBlockSumsBuffer(std::move(scannedBlockSumsBuffer))
    , m_totalCountBuffer(std::move(totalCountBuffer)) {
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"RasterBucketsBlockOffsetsCS",
        {},
        "CLod_RasterBucketsBlockOffsetsPSO");
}

void RasterBucketBlockOffsetsPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithShaderResource(m_blockSumsBuffer)
        .WithUnorderedAccess(m_offsetsBuffer, m_scannedBlockSumsBuffer, m_totalCountBuffer);
}

void RasterBucketBlockOffsetsPass::Setup() {}

PassReturn RasterBucketBlockOffsetsPass::Execute(PassExecutionContext& executionContext) {
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;

    auto& commandList = executionContext.commandList;
    auto& pm = PSOManager::GetInstance();

    auto numBuckets = context.materialManager->GetRasterBucketCount();
    const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(pm.GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    uint32_t rc[NumMiscUintRootConstants] = {};
    rc[UintRootConstant0] = numBuckets;
    rc[UintRootConstant1] = numBlocks;
    rc[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_blockSumsBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_RASTER_BUCKETS_SCANNED_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_scannedBlockSumsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_RASTER_BUCKETS_TOTAL_COUNT_DESCRIPTOR_INDEX] = m_totalCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rc);

    commandList.Dispatch(1, 1, 1);
    return {};
}

void RasterBucketBlockOffsetsPass::Update(const UpdateExecutionContext& executionContext) {
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    auto numBuckets = context.materialManager->GetRasterBucketCount();
    const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

    if (m_scannedBlockSumsBuffer->GetSize() < static_cast<size_t>(numBlocks) * sizeof(uint32_t)) {
        m_scannedBlockSumsBuffer->ResizeStructured(numBlocks);
    }
}

void RasterBucketBlockOffsetsPass::Cleanup() {}
