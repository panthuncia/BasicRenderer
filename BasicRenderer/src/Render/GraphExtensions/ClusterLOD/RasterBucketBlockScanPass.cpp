#include "Render/GraphExtensions/ClusterLOD/RasterBucketBlockScanPass.h"

#include "Managers/MaterialManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "../shaders/PerPassRootConstants/clodPrefixScanRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

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

void RasterBucketBlockScanPass::Declare(org::PassBuilder& builder) {
    builder.WithShaderResource(m_histogramBuffer)
        .WithUnorderedAccess(m_offsetsBuffer, m_blockSumsBuffer);
    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}


br::render::PreparedComputeDispatch RasterBucketBlockScanPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    br::render::PreparedComputeDispatch data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    data.program = preparation.CaptureProgram(m_pso);
    data.descriptorIndices = CaptureResourceDescriptorIndices(m_pso.GetResourceDescriptorSlots());
    const auto numBuckets = context->materialManager->GetRasterBucketCount();
    data.constants[UintRootConstant0] = numBuckets;
    data.constants[CLOD_PREFIX_SCAN_NUM_BUCKETS] = numBuckets;
    data.constants[CLOD_PREFIX_SCAN_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_PREFIX_SCAN_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_PREFIX_SCAN_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX] = m_blockSumsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    const bool enabled = !m_runWhenComputeSWRasterEnabledOnly
        || CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)());
    data.groupsX = enabled ? (numBuckets + m_blockSize - 1u) / m_blockSize : 0u;
    return data;
}

void RasterBucketBlockScanPass::Update(const UpdateExecutionContext& executionContext) {
    if (m_runWhenComputeSWRasterEnabledOnly && !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
        return;
    }

    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;
    auto numBuckets = context.preparedRasterBucketCount;
    const uint32_t numBlocks = (numBuckets + m_blockSize - 1) / m_blockSize;

    if (m_offsetsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(uint32_t)) {
        m_offsetsBuffer->ResizeStructured(numBuckets);
    }
    if (m_blockSumsBuffer->GetSize() < static_cast<size_t>(numBlocks) * sizeof(uint32_t)) {
        m_blockSumsBuffer->ResizeStructured(numBlocks);
    }
}

