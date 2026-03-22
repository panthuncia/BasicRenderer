#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockScanPass.h"

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "../shaders/PerPassRootConstants/clodPrefixScanRootConstants.h"

RasterBucketBlockScanPass::RasterBucketBlockScanPass(
    std::shared_ptr<Buffer> histogramBuffer,
    std::shared_ptr<Buffer> offsetsBuffer,
    std::shared_ptr<Buffer> blockSumsBuffer,
    bool runWhenComputeSWRasterEnabledOnly)
    : m_histogramBuffer(std::move(histogramBuffer))
    , m_offsetsBuffer(std::move(offsetsBuffer))
    , m_blockSumsBuffer(std::move(blockSumsBuffer))
    , m_runWhenComputeSWRasterEnabledOnly(runWhenComputeSWRasterEnabledOnly) {
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"RasterBucketsBlockScanCS",
        {},
        "CLod_RasterBucketsBlockScanPSO");
}

void RasterBucketBlockScanPass::DeclareResourceUsages(ComputePassBuilder* builder) {
    builder->WithShaderResource(m_histogramBuffer)
        .WithUnorderedAccess(m_offsetsBuffer, m_blockSumsBuffer);
}

void RasterBucketBlockScanPass::Setup() {}

PassReturn RasterBucketBlockScanPass::Execute(PassExecutionContext& executionContext) {
    if (m_runWhenComputeSWRasterEnabledOnly && !SettingsManager::GetInstance().getSettingGetter<bool>("useComputeSwRaster")()) {
        return {};
    }

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
    rc[CLOD_PREFIX_SCAN_NUM_BUCKETS] = numBuckets;
    rc[CLOD_PREFIX_SCAN_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
    rc[CLOD_PREFIX_SCAN_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rc[CLOD_PREFIX_SCAN_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_blockSumsBuffer->GetUAVShaderVisibleInfo(0).slot.index;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rc);

    commandList.Dispatch(numBlocks, 1, 1);
    return {};
}

void RasterBucketBlockScanPass::Update(const UpdateExecutionContext& executionContext) {
    if (m_runWhenComputeSWRasterEnabledOnly && !SettingsManager::GetInstance().getSettingGetter<bool>("useComputeSwRaster")()) {
        return;
    }

    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    auto numBuckets = context.materialManager->GetRasterBucketCount();
    const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

    if (m_offsetsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
        m_offsetsBuffer->ResizeStructured(numBuckets);
    }
    if (m_blockSumsBuffer->GetSize() < static_cast<size_t>(numBlocks) * sizeof(uint32_t)) {
        m_blockSumsBuffer->ResizeStructured(numBlocks);
    }
}

void RasterBucketBlockScanPass::Cleanup() {}
