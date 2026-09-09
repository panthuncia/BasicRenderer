#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapBuildPageListsPass.h"

#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "ShaderBuffers.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowBuildPageListsRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapBuildPageListsPass::VirtualShadowMapBuildPageListsPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> allocationCountBuffer,
    std::shared_ptr<Buffer> freePhysicalPagesBuffer,
    std::shared_ptr<Buffer> reusablePhysicalPagesBuffer,
    std::shared_ptr<Buffer> pageListHeaderBuffer)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_allocationCountBuffer(std::move(allocationCountBuffer))
    , m_freePhysicalPagesBuffer(std::move(freePhysicalPagesBuffer))
    , m_reusablePhysicalPagesBuffer(std::move(reusablePhysicalPagesBuffer))
    , m_pageListHeaderBuffer(std::move(pageListHeaderBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowBuildPageListsCSMain",
        {},
        "CLod.VirtualShadow.BuildPageLists.PSO");
}

void VirtualShadowMapBuildPageListsPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_pageTableTexture, m_pageMetadataBuffer, m_allocationCountBuffer)
        .WithUnorderedAccess(
            m_freePhysicalPagesBuffer,
            m_reusablePhysicalPagesBuffer,
            m_pageListHeaderBuffer);

    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}

void VirtualShadowMapBuildPageListsPass::Initialize() {}



br::render::PreparedComputeDispatch VirtualShadowMapBuildPageListsPass::Prepare(const org::PassPrepareContext& preparation) {
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
    data.constants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_FREE_PAGES_DESCRIPTOR_INDEX] = m_freePhysicalPagesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_REUSABLE_PAGES_DESCRIPTOR_INDEX] = m_reusablePhysicalPagesBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_HEADER_DESCRIPTOR_INDEX] = m_pageListHeaderBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PHYSICAL_PAGE_COUNT] = config.maxPhysicalPages;
    data.constants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
    data.constants[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_ALLOCATION_COUNT_DESCRIPTOR_INDEX] = m_allocationCountBuffer->GetSRVInfo(0).slot.index;
    data.groupsX = 1;
    return data;
}

void VirtualShadowMapBuildPageListsPass::ShutdownPass() {}

void VirtualShadowMapBuildPageListsPass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
