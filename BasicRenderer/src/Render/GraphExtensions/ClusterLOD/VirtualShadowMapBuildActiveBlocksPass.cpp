#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapBuildActiveBlocksPass.h"

#include "BuiltinResources.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowBuildActiveBlocksRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapBuildActiveBlocksPass::VirtualShadowMapBuildActiveBlocksPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<Buffer> activeBlockMetadataBuffer,
    bool dynamicPages)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_activeBlockMetadataBuffer(std::move(activeBlockMetadataBuffer))
    , m_dynamicPages(dynamicPages)
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowBuildActiveBlocksCSMain",
        { { L"CLOD_VSM_TWO_LAYER_ACTIVE_BLOCKS_VERSION", L"2" } },
        "CLod.VirtualShadow.BuildActiveBlocks.PSO");
}

void VirtualShadowMapBuildActiveBlocksPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_pageTableTexture, m_clipmapInfoBuffer)
        .WithUnorderedAccess(m_activeBlockMetadataBuffer)
        .WithConstantBuffer(Builtin::PerFrameBuffer);
}



br::render::PreparedComputeDispatch VirtualShadowMapBuildActiveBlocksPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    auto payload = m_pso.GetPayload();
    br::render::PreparedComputeDispatch data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    auto program = preparation.CaptureProgramBinding(std::move(payload));
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_VSM_BUILD_ACTIVE_BLOCKS_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index;
    data.constants[CLOD_VSM_BUILD_ACTIVE_BLOCKS_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    data.constants[CLOD_VSM_BUILD_ACTIVE_BLOCKS_OUTPUT_DESCRIPTOR_INDEX] = m_activeBlockMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VSM_BUILD_ACTIVE_BLOCKS_COUNT] = CLodVirtualShadowMaxMarkedBlockCount;
    data.constants[CLOD_VSM_BUILD_ACTIVE_BLOCKS_DYNAMIC] = m_dynamicPages ? 1u : 0u;
    data.groupsX = (CLodVirtualShadowMaxMarkedBlockCount + 63u) / 64u;
    return data;
}

void VirtualShadowMapBuildActiveBlocksPass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
