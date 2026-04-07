#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapConsumePredictedPagesPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowConsumePredictedPagesRootConstants.h"

VirtualShadowMapConsumePredictedPagesPass::VirtualShadowMapConsumePredictedPagesPass(
    std::shared_ptr<Buffer> predictedPagesBuffer,
    std::shared_ptr<Buffer> predictedPageCountBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> directionalPageViewInfoBuffer)
    : m_predictedPagesBuffer(std::move(predictedPagesBuffer))
    , m_predictedPageCountBuffer(std::move(predictedPageCountBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_pageTableTexture(std::move(pageTableTexture))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_directionalPageViewInfoBuffer(std::move(directionalPageViewInfoBuffer))
{
    m_consumePso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowConsumePredictedPagesCSMain",
        {},
        "CLod.VirtualShadow.ConsumePredictedPages.PSO");

    m_clearCountPso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowClearConsumedPredictedPageCountCSMain",
        {},
        "CLod.VirtualShadow.ClearConsumedPredictedPageCount.PSO");
}

void VirtualShadowMapConsumePredictedPagesPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(
            m_predictedPagesBuffer,
            m_predictedPageCountBuffer,
            m_clipmapInfoBuffer)
        .WithUnorderedAccess(
            m_predictedPageCountBuffer,
            m_pageTableTexture,
            m_dirtyPageFlagsBuffer,
            m_pageMetadataBuffer,
            m_directionalPageViewInfoBuffer);
}

void VirtualShadowMapConsumePredictedPagesPass::Setup() {}

PassReturn VirtualShadowMapConsumePredictedPagesPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    BindResourceDescriptorIndices(commandList, m_consumePso.GetResourceDescriptorSlots());

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_CONSUME_PREDICTED_PAGES_INPUT_DESCRIPTOR_INDEX] = m_predictedPagesBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CONSUME_PREDICTED_PAGE_COUNT_INPUT_DESCRIPTOR_INDEX] = m_predictedPageCountBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CONSUME_PREDICTED_PAGE_COUNT_CLEAR_DESCRIPTOR_INDEX] = m_predictedPageCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CONSUME_PREDICTED_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CONSUME_PREDICTED_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CONSUME_PREDICTED_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CONSUME_PREDICTED_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_CONSUME_PREDICTED_PAGE_VIEW_INFO_DESCRIPTOR_INDEX] = m_directionalPageViewInfoBuffer->GetUAVShaderVisibleInfo(0).slot.index;

    commandList.BindPipeline(m_consumePso.GetAPIPipelineState().GetHandle());
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    constexpr uint32_t kThreadsPerGroup = 64u;
    commandList.Dispatch((CLodVirtualShadowPredictedPageListCapacity() + kThreadsPerGroup - 1u) / kThreadsPerGroup, 1u, 1u);

    rhi::GlobalBarrier globalBarrier{};
    globalBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
    globalBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;
    globalBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    globalBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    rhi::BarrierBatch barrierBatch{};
    barrierBatch.globals = rhi::Span<rhi::GlobalBarrier>(&globalBarrier, 1);
    commandList.Barriers(barrierBatch);

    BindResourceDescriptorIndices(commandList, m_clearCountPso.GetResourceDescriptorSlots());
    commandList.BindPipeline(m_clearCountPso.GetAPIPipelineState().GetHandle());
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);
    commandList.Dispatch(1u, 1u, 1u);

    return {};
}

void VirtualShadowMapConsumePredictedPagesPass::Cleanup() {}