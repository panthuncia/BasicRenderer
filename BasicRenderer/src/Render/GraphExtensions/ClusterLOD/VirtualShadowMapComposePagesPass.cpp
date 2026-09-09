#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapComposePagesPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowComposeRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapComposePagesPass::VirtualShadowMapComposePagesPass(
    std::shared_ptr<PixelBuffer> staticPagesTexture,
    std::shared_ptr<PixelBuffer> dynamicPagesTexture,
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> statsBuffer)
    : m_staticPagesTexture(std::move(staticPagesTexture))
    , m_dynamicPagesTexture(std::move(dynamicPagesTexture))
    , m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_statsBuffer(std::move(statsBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowComposePhysicalPagesCSMain",
        { { L"CLOD_VSM_TWO_LAYER_COMPOSE_VERSION", L"2" } },
        "CLod.VirtualShadow.ComposePhysicalPages.PSO");
}

void VirtualShadowMapComposePagesPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(
            m_staticPagesTexture,
            m_pageTableTexture,
            m_pageMetadataBuffer)
        .WithUnorderedAccess(m_dynamicPagesTexture, m_statsBuffer);
}



br::render::PreparedComputeDispatch VirtualShadowMapComposePagesPass::Prepare(const org::PassPrepareContext& preparation) {
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
    data.constants[CLOD_VIRTUAL_SHADOW_COMPOSE_STATIC_PAGES_DESCRIPTOR_INDEX] = m_staticPagesTexture->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_COMPOSE_DYNAMIC_PAGES_DESCRIPTOR_INDEX] = m_dynamicPagesTexture->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_COMPOSE_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_COMPOSE_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_COMPOSE_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
    data.constants[CLOD_VIRTUAL_SHADOW_COMPOSE_PHYSICAL_PAGE_COUNT] = config.maxPhysicalPages;
    data.constants[CLOD_VIRTUAL_SHADOW_COMPOSE_PHYSICAL_ATLAS_PAGES_WIDE] = config.physicalAtlasPagesWide;
    data.constants[CLOD_VIRTUAL_SHADOW_COMPOSE_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.groupsX = config.maxPhysicalPages;
    return data;
}

void VirtualShadowMapComposePagesPass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
