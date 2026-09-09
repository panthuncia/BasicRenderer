#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapDeduplicatePredictedPagesPass.h"

#include "BuiltinResources.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowDeduplicatePredictedPagesRootConstants.h"
#include "Render/ShaderAPI.h"

VirtualShadowMapDeduplicatePredictedPagesPass::VirtualShadowMapDeduplicatePredictedPagesPass(
    std::shared_ptr<Buffer> predictiveRawPagesBuffer,
    std::shared_ptr<Buffer> predictiveRawPageCountBuffer,
    std::shared_ptr<Buffer> predictedScratchBitsetBuffer,
    std::shared_ptr<Buffer> predictedPagesBuffer,
    std::shared_ptr<Buffer> predictedPageCountBuffer,
    std::shared_ptr<Buffer> statsBuffer,
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> dirtyFlagsBuffer,
    uint32_t physicalPageCount)
    : m_predictiveRawPagesBuffer(std::move(predictiveRawPagesBuffer))
    , m_predictiveRawPageCountBuffer(std::move(predictiveRawPageCountBuffer))
    , m_predictedScratchBitsetBuffer(std::move(predictedScratchBitsetBuffer))
    , m_predictedPagesBuffer(std::move(predictedPagesBuffer))
    , m_predictedPageCountBuffer(std::move(predictedPageCountBuffer))
    , m_statsBuffer(std::move(statsBuffer))
    , m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_dirtyFlagsBuffer(std::move(dirtyFlagsBuffer))
    , m_physicalPageCount(physicalPageCount)
{
    m_clearStatePso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowClearPredictedPageDedupStateCSMain",
        {},
        "CLod.VirtualShadow.ClearPredictedPageDedupState.PSO");

    m_deduplicatePso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowDeduplicatePredictedPagesCSMain",
        {},
        "CLod.VirtualShadow.DeduplicatePredictedPages.PSO");
}

void VirtualShadowMapDeduplicatePredictedPagesPass::Declare(org::PassBuilder& declaration)
{
    declaration.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    auto* builder = &declaration;
    builder->WithShaderResource(
            m_predictiveRawPagesBuffer,
            m_predictiveRawPageCountBuffer)
        .WithConstantBuffer(Builtin::PerFrameBuffer)
        .WithUnorderedAccess(
            m_predictedScratchBitsetBuffer,
            m_predictedPagesBuffer,
            m_predictedPageCountBuffer,
            m_statsBuffer,
            m_pageTableTexture,
            m_pageMetadataBuffer,
            m_dirtyFlagsBuffer);
}



br::render::PreparedComputePipelineSequence VirtualShadowMapDeduplicatePredictedPagesPass::Prepare(const org::PassPrepareContext& preparation)
{
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    br::render::PreparedComputePipelineSequence data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.steps.resize(2);
    auto clear = preparation.CaptureProgramBinding(m_clearStatePso);
    data.steps[0].program = clear.program;
    data.steps[0].descriptorIndices = std::move(clear.descriptorIndices);
    auto deduplicate = preparation.CaptureProgramBinding(m_deduplicatePso);
    data.steps[1].program = deduplicate.program;
    data.steps[1].descriptorIndices = std::move(deduplicate.descriptorIndices);
    auto& c = data.steps[0].constants;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_RAW_PAGES_DESCRIPTOR_INDEX] = m_predictiveRawPagesBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_RAW_PAGE_COUNT_DESCRIPTOR_INDEX] = m_predictiveRawPageCountBuffer->GetSRVInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_SCRATCH_BITSET_DESCRIPTOR_INDEX] = m_predictedScratchBitsetBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_OUTPUT_PAGES_DESCRIPTOR_INDEX] = m_predictedPagesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_OUTPUT_PAGE_COUNT_DESCRIPTOR_INDEX] = m_predictedPageCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    c[CLOD_VIRTUAL_SHADOW_DEDUPLICATE_PHYSICAL_PAGE_COUNT] = m_physicalPageCount;
    data.steps[0].groupsX = (CLodVirtualShadowFallbackDependencyHashCapacity + 63u) / 64u;
    data.steps[1].groupsX = (CLodVirtualShadowPredictiveRawPageCapacity + 63u) / 64u;
    data.steps[1].constants = c;
    data.steps[0].uavBarrierAfter = data.steps[1].uavBarrierAfter = true;
    return data;
}

void VirtualShadowMapDeduplicatePredictedPagesPass::Record(const br::render::PreparedComputePipelineSequence& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputePipelineSequence(data, recording);
}
