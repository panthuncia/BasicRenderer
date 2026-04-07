#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapMarkPagesPass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "BuiltinResources.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowMarkRootConstants.h"

VirtualShadowMapMarkPagesPass::VirtualShadowMapMarkPagesPass(
    std::shared_ptr<Buffer> tileWorkBuffer,
    std::shared_ptr<Buffer> tileCountBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> allocationRequestsBuffer,
    std::shared_ptr<Buffer> allocationCountBuffer,
    std::shared_ptr<Buffer> markClipmapDataBuffer,
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
    std::shared_ptr<Buffer> directionalPageViewInfoBuffer,
    std::shared_ptr<Buffer> statsBuffer)
    : m_tileWorkBuffer(std::move(tileWorkBuffer))
    , m_tileCountBuffer(std::move(tileCountBuffer))
    , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
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
        L"CLodVirtualShadowMarkPagesCSMain",
        {},
        "CLod.VirtualShadow.MarkPages.PSO");

    rhi::IndirectArg dispatchArgs[] = {
        {.kind = rhi::IndirectArgKind::Dispatch }
    };

    auto device = DeviceManager::GetInstance().GetDevice();
    device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(dispatchArgs, 1), sizeof(CLodReyesDispatchIndirectCommand) },
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        m_commandSignature);
}

void VirtualShadowMapMarkPagesPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(
            Builtin::Shadows::CLodCompactMainCamera,
            m_tileWorkBuffer,
            m_tileCountBuffer,
            m_markClipmapDataBuffer)
        .WithIndirectArguments(m_indirectArgsBuffer)
        .WithUnorderedAccess(
            m_allocationCountBuffer,
            m_allocationRequestsBuffer,
            m_pageTableTexture,
            m_dirtyPageFlagsBuffer,
            m_directionalPageViewInfoBuffer,
            m_statsBuffer);
}

void VirtualShadowMapMarkPagesPass::Setup() {}

void VirtualShadowMapMarkPagesPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;
    m_activeClipmapCount = (std::min)(
        static_cast<uint32_t>(SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")()),
    CLodVirtualShadowMaxSupportedClipmapCount);
}

PassReturn VirtualShadowMapMarkPagesPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());
    const uint32_t virtualShadowResolution = CLodVirtualShadowSanitizeVirtualResolution(
        SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodVirtualShadowVirtualResolutionSettingName)());
    const uint32_t virtualShadowPhysicalPageCount =
        CLodVirtualShadowPhysicalPageCountFromVirtualResolution(virtualShadowResolution);

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_REQUESTS_DESCRIPTOR_INDEX] = m_allocationRequestsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_REQUEST_COUNT_DESCRIPTOR_INDEX] = m_allocationCountBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_TILE_WORK_DESCRIPTOR_INDEX] = m_tileWorkBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_TILE_COUNT_DESCRIPTOR_INDEX] = m_tileCountBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_CLIPMAP_DATA_DESCRIPTOR_INDEX] = m_markClipmapDataBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_SCREEN_WIDTH] = context.renderResolution.x;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_SCREEN_HEIGHT] = context.renderResolution.y;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_RESOLUTION] = CLodVirtualShadowPageTableResolutionFromVirtualResolution(virtualShadowResolution);
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_MAX_REQUEST_COUNT] = (std::min)(virtualShadowPhysicalPageCount, CLodVirtualShadowMaxAllocationRequests);
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_PAGE_VIEW_INFO_DESCRIPTOR_INDEX] = m_directionalPageViewInfoBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_MARK_ACTIVE_CLIPMAP_COUNT] = m_activeClipmapCount;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    commandList.ExecuteIndirect(m_commandSignature->GetHandle(), m_indirectArgsBuffer->GetAPIResource().GetHandle(), 0, {}, 0, 1);

    return {};
}

void VirtualShadowMapMarkPagesPass::Cleanup() {}