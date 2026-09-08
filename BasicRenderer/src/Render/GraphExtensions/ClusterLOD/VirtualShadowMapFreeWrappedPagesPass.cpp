#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapFreeWrappedPagesPass.h"

#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowFreeWrappedRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapFreeWrappedPagesPass::VirtualShadowMapFreeWrappedPagesPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<Buffer> statsBuffer)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_statsBuffer(std::move(statsBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowFreeWrappedPagesCSMain",
        {},
        "CLod.VirtualShadow.FreeWrappedPages.PSO");
}

void VirtualShadowMapFreeWrappedPagesPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(m_clipmapInfoBuffer)
        .WithUnorderedAccess(
            m_pageTableTexture,
            m_pageMetadataBuffer,
            m_statsBuffer);
}

void VirtualShadowMapFreeWrappedPagesPass::Setup() {}

PassReturn VirtualShadowMapFreeWrappedPagesPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());
    const CLodVirtualShadowResolutionConfig virtualShadowConfig = CLodVirtualShadowBuildRuntimeResolutionConfig();

    uint32_t rootConstants[NumMiscUintRootConstants] = {};
    rootConstants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_PAGE_TABLE_DESCRIPTOR_INDEX] =
        m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_PAGE_METADATA_DESCRIPTOR_INDEX] =
        m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_CLIPMAP_INFO_DESCRIPTOR_INDEX] =
        m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_STATS_DESCRIPTOR_INDEX] =
        m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    rootConstants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_PAGE_TABLE_RESOLUTION] = virtualShadowConfig.pageTableResolution;
    rootConstants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
    rootConstants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_PHYSICAL_PAGE_COUNT] = virtualShadowConfig.maxPhysicalPages;

    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        rootConstants);

    const uint32_t groupsX = (virtualShadowConfig.pageTableResolution + 7u) / 8u;
    const uint32_t groupsY = (virtualShadowConfig.pageTableResolution + 7u) / 8u;
    commandList.Dispatch(groupsX, groupsY, CLodVirtualShadowMaxSupportedClipmapCount);
    return {};
}

PreparedPass VirtualShadowMapFreeWrappedPagesPass::PrepareFrame(FramePreparationContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    auto payload = m_pso.GetPayload();
    br::render::PreparedComputeDispatch data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle(); data.pipeline = payload->pso.Get().GetHandle();
    data.pipelineOwner = std::move(payload); data.descriptorIndices = CaptureResourceDescriptorIndices(data.pipelineOwner->pipelineResources);
    data.constants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
    data.constants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
    data.constants[CLOD_VIRTUAL_SHADOW_FREE_WRAPPED_PHYSICAL_PAGE_COUNT] = config.maxPhysicalPages;
    data.groupsX = (config.pageTableResolution + 7u) / 8u; data.groupsY = data.groupsX; data.groupsZ = CLodVirtualShadowMaxSupportedClipmapCount;
    return PreparedPass::MakeOwned(std::move(data), &br::render::RecordPreparedComputeDispatch);
}

void VirtualShadowMapFreeWrappedPagesPass::Cleanup() {}
