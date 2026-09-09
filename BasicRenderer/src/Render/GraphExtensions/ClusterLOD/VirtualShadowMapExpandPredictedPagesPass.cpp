#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapExpandPredictedPagesPass.h"

#include "BuiltinResources.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowExpandPredictedPagesRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapExpandPredictedPagesPass::VirtualShadowMapExpandPredictedPagesPass(
    std::shared_ptr<Buffer> predictiveCandidatesBuffer,
    std::shared_ptr<Buffer> predictiveCandidateCountBuffer,
    std::shared_ptr<Buffer> predictiveRawPagesBuffer,
    std::shared_ptr<Buffer> predictiveRawPageCountBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<Buffer> scratchBitsetBuffer,
    std::shared_ptr<Buffer> statsBuffer,
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> pageViewInfoBuffer,
    uint32_t physicalPageCount)
    : m_predictiveCandidatesBuffer(std::move(predictiveCandidatesBuffer))
    , m_predictiveCandidateCountBuffer(std::move(predictiveCandidateCountBuffer))
    , m_predictiveRawPagesBuffer(std::move(predictiveRawPagesBuffer))
    , m_predictiveRawPageCountBuffer(std::move(predictiveRawPageCountBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_scratchBitsetBuffer(std::move(scratchBitsetBuffer))
    , m_statsBuffer(std::move(statsBuffer))
    , m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_pageViewInfoBuffer(std::move(pageViewInfoBuffer))
    , m_physicalPageCount(physicalPageCount)
{
    spdlog::info("VirtualShadowMapExpandPredictedPagesPass: stamp pipeline begin");
    m_stampContentGenerationPso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowStampRenderedPageGenerationsCSMain",
        {},
        "CLod.VirtualShadow.StampRenderedPageGenerations.PSO");
    spdlog::info("VirtualShadowMapExpandPredictedPagesPass: stamp pipeline complete; expand pipeline begin");
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowExpandPredictedPagesCSMain",
        {},
        "CLod.VirtualShadow.ExpandPredictedPages.PSO");
    spdlog::info("VirtualShadowMapExpandPredictedPagesPass: expand pipeline complete; reset pipeline begin");
    m_resetCandidateCountPso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowResetFallbackCandidateCountCSMain",
        {},
        "CLod.VirtualShadow.ResetFallbackCandidateCount.PSO");
    spdlog::info("VirtualShadowMapExpandPredictedPagesPass: reset pipeline complete");
}

void VirtualShadowMapExpandPredictedPagesPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(
            Builtin::Shadows::CLodCompactShadowCameras,
            Builtin::CameraBuffer,
            m_clipmapInfoBuffer)
        .WithUnorderedAccess(
            m_predictiveCandidatesBuffer,
            m_predictiveCandidateCountBuffer,
            m_predictiveRawPagesBuffer,
            m_predictiveRawPageCountBuffer,
            m_scratchBitsetBuffer,
            m_statsBuffer,
            m_pageTableTexture,
            m_pageMetadataBuffer,
            m_pageViewInfoBuffer);

    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}

br::render::PreparedComputePipelineSequence VirtualShadowMapExpandPredictedPagesPass::Prepare(const org::PassPrepareContext& preparation)
{
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    br::render::PreparedComputePipelineSequence data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    std::array<unsigned int, NumMiscUintRootConstants> constants{};
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_CANDIDATES_DESCRIPTOR_INDEX] = m_predictiveCandidatesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_CANDIDATE_COUNT_DESCRIPTOR_INDEX] = m_predictiveCandidateCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_RAW_PAGES_DESCRIPTOR_INDEX] = m_predictiveRawPagesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_RAW_PAGE_COUNT_DESCRIPTOR_INDEX] = m_predictiveRawPageCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_SCRATCH_BITSET_DESCRIPTOR_INDEX] = m_scratchBitsetBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_PHYSICAL_PAGE_COUNT] = m_physicalPageCount;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
    constants[CLOD_VIRTUAL_SHADOW_EXPAND_PREDICTED_PAGES_PAGE_VIEW_INFO_DESCRIPTOR_INDEX] = m_pageViewInfoBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    const auto append = [&](const PipelineState& pso, uint32_t groups, bool barrierBefore) {
        br::render::PreparedComputePipelineSequence::Step step{};
        auto program = preparation.CaptureProgramBinding(pso);
        step.program = program.program;
        step.descriptorIndices = std::move(program.descriptorIndices);
        step.constants = constants;
        step.groupsX = groups;
        step.uavBarrierBefore = barrierBefore;
        data.steps.push_back(std::move(step));
    };
    append(m_stampContentGenerationPso, (m_physicalPageCount + 63u) / 64u, false);
    append(m_pso, (CLodVirtualShadowPredictiveCandidateCapacity + 63u) / 64u, true);
    append(m_resetCandidateCountPso, 1u, true);
    return data;
}

void VirtualShadowMapExpandPredictedPagesPass::Record(const br::render::PreparedComputePipelineSequence& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputePipelineSequence(data, recording);
}
