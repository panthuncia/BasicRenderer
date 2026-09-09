#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearPagesPass.h"

#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowClearRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapClearPagesPass::VirtualShadowMapClearPagesPass(
    std::shared_ptr<PixelBuffer> staticPagesTexture,
    std::shared_ptr<PixelBuffer> dynamicPagesTexture,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<Buffer> pageViewInfoBuffer,
    std::shared_ptr<Buffer> statsBuffer)
    : m_staticPagesTexture(std::move(staticPagesTexture))
    , m_dynamicPagesTexture(std::move(dynamicPagesTexture))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
    , m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_pageViewInfoBuffer(std::move(pageViewInfoBuffer))
    , m_statsBuffer(std::move(statsBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowClearPhysicalPagesCSMain",
        { { L"CLOD_VSM_TWO_LAYER_CLEAR_VERSION", L"2" } },
        "CLod.VirtualShadow.ClearPhysicalPages.PSO");
}

void VirtualShadowMapClearPagesPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithUnorderedAccess(
        m_staticPagesTexture,
        m_dynamicPagesTexture,
        m_dirtyPageFlagsBuffer,
        m_pageTableTexture,
        m_pageMetadataBuffer,
        m_pageViewInfoBuffer,
        m_statsBuffer);

    builder.WithShaderResource(
            m_clipmapInfoBuffer,
            Builtin::CameraBuffer)
        .WithConstantBuffer(Builtin::PerFrameBuffer);
}

void VirtualShadowMapClearPagesPass::Initialize()
{
}



br::render::PreparedComputeDispatch VirtualShadowMapClearPagesPass::Prepare(const org::PassPrepareContext& preparation)
{
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    auto payload = m_pso.GetPayload();
    br::render::PreparedComputeDispatch data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    auto program = preparation.CaptureProgramBinding(std::move(payload));
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_STATIC_PAGES_DESCRIPTOR_INDEX] = m_staticPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_DYNAMIC_PAGES_DESCRIPTOR_INDEX] = m_dynamicPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_PHYSICAL_PAGE_COUNT] = config.maxPhysicalPages;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_PHYSICAL_ATLAS_PAGES_WIDE] = config.physicalAtlasPagesWide;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_PAGE_VIEW_INFO_DESCRIPTOR_INDEX] = m_pageViewInfoBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_DYNAMIC_CONTENT_FILTER_ENABLED] = SettingsManager::GetInstance().getSettingGetter<bool>(CLodDirectionalVirtualShadowDynamicContentFilterSettingName)() ? 1u : 0u;
    data.groupsX = config.maxPhysicalPages;
    return data;
}

void VirtualShadowMapClearPagesPass::ShutdownPass()
{
}

void VirtualShadowMapClearPagesPass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
