#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapResolveMarkedBlocksPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowResolveMarkedBlocksRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapResolveMarkedBlocksPass::VirtualShadowMapResolveMarkedBlocksPass(
    std::shared_ptr<Buffer> markedBlocksMaskBuffer,
    std::shared_ptr<Buffer> markedBlocksListBuffer,
    std::shared_ptr<Buffer> markedBlocksCountBuffer,
    std::shared_ptr<Buffer> allocationRequestsBuffer,
    std::shared_ptr<Buffer> allocationCountBuffer,
    std::shared_ptr<Buffer> markClipmapDataBuffer,
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
    std::shared_ptr<Buffer> directionalPageViewInfoBuffer,
    std::shared_ptr<Buffer> statsBuffer)
    : m_markedBlocksMaskBuffer(std::move(markedBlocksMaskBuffer))
    , m_markedBlocksListBuffer(std::move(markedBlocksListBuffer))
    , m_markedBlocksCountBuffer(std::move(markedBlocksCountBuffer))
    , m_allocationRequestsBuffer(std::move(allocationRequestsBuffer))
    , m_allocationCountBuffer(std::move(allocationCountBuffer))
    , m_markClipmapDataBuffer(std::move(markClipmapDataBuffer))
    , m_pageTableTexture(std::move(pageTableTexture))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
    , m_directionalPageViewInfoBuffer(std::move(directionalPageViewInfoBuffer))
    , m_statsBuffer(std::move(statsBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowResolveMarkedBlocksCSMain",
        {},
        "CLod.VirtualShadow.ResolveMarkedBlocks.PSO");
}

void VirtualShadowMapResolveMarkedBlocksPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(
            m_markedBlocksMaskBuffer,
            m_markedBlocksListBuffer,
            m_markedBlocksCountBuffer,
            m_markClipmapDataBuffer)
        .WithUnorderedAccess(
            m_allocationRequestsBuffer,
            m_allocationCountBuffer,
            m_pageTableTexture,
            m_dirtyPageFlagsBuffer,
            m_directionalPageViewInfoBuffer,
            m_statsBuffer);
}

void VirtualShadowMapResolveMarkedBlocksPass::Initialize() {}

void VirtualShadowMapResolveMarkedBlocksPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
    m_activeClipmapCount = (std::min)(
        static_cast<uint32_t>(SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")()),
        CLodVirtualShadowMaxSupportedClipmapCount);
}



br::render::PreparedComputeDispatch VirtualShadowMapResolveMarkedBlocksPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    auto payload = m_pso.GetPayload(); br::render::PreparedComputeDispatch data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle(); auto program = preparation.CaptureProgramBinding(std::move(payload));
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_MASK_DESCRIPTOR_INDEX] = m_markedBlocksMaskBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_LIST_DESCRIPTOR_INDEX] = m_markedBlocksListBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_COUNT_DESCRIPTOR_INDEX] = m_markedBlocksCountBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_REQUESTS_DESCRIPTOR_INDEX] = m_allocationRequestsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_REQUEST_COUNT_DESCRIPTOR_INDEX] = m_allocationCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_PAGE_VIEW_INFO_DESCRIPTOR_INDEX] = m_directionalPageViewInfoBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_ACTIVE_CLIPMAP_COUNT] = m_activeClipmapCount;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_CLIPMAP_DATA_DESCRIPTOR_INDEX] = m_markClipmapDataBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_RESOLVE_MARKED_BLOCKS_MAX_REQUEST_COUNT] = config.maxAllocationRequests;
    data.groupsX = (CLodVirtualShadowMaxMarkedBlockCount + 63u) / 64u;
    return data;
}

void VirtualShadowMapResolveMarkedBlocksPass::ShutdownPass() {}

void VirtualShadowMapResolveMarkedBlocksPass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
