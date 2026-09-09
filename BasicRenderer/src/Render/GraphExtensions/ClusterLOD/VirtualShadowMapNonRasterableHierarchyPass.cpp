#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapNonRasterableHierarchyPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowDirtyHierarchyRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapNonRasterableHierarchyPass::VirtualShadowMapNonRasterableHierarchyPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<PixelBuffer> nonRasterableHierarchyTexture,
    std::shared_ptr<Buffer> clipmapInfoBuffer)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_nonRasterableHierarchyTexture(std::move(nonRasterableHierarchyTexture))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowBuildNonRasterableHierarchyCSMain",
        {},
        "CLod.VirtualShadow.NonRasterableHierarchy.PSO");
}

void VirtualShadowMapNonRasterableHierarchyPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(m_pageTableTexture, Subresources(m_pageTableTexture, Mip{0, 1}))
        .WithShaderResource(m_clipmapInfoBuffer)
        .WithUnorderedAccess(Subresources(m_nonRasterableHierarchyTexture, FromMip{0}));

    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}

br::render::PreparedComputeDispatchSequence VirtualShadowMapNonRasterableHierarchyPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    br::render::PreparedComputeDispatchSequence data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle(); auto program = preparation.CaptureProgramBinding(m_pso);
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    const uint32_t mipCount = m_nonRasterableHierarchyTexture->GetNumUAVMipLevels(); data.steps.reserve(mipCount);
    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        const bool pageTable = mip == 0; const uint32_t src = pageTable ? config.pageTableResolution : (std::max)(config.pageTableResolution >> (mip - 1u), 1u);
        const uint32_t dst = pageTable ? src : (src > 1u ? src >> 1u : 1u); br::render::PreparedComputeDispatchSequence::Step step{};
        step.uavBarrierBefore = !pageTable;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_DESCRIPTOR_INDEX] = pageTable ? m_pageTableTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index : m_nonRasterableHierarchyTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, mip - 1u).slot.index;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_DEST_DESCRIPTOR_INDEX] = m_nonRasterableHierarchyTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, mip).slot.index;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_IS_PAGE_TABLE] = pageTable ? 1u : 0u;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION] = src;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
        step.constants[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
        step.groupsX = (dst + 7u) / 8u; step.groupsY = step.groupsX; step.groupsZ = CLodVirtualShadowMaxSupportedClipmapCount; data.steps.push_back(std::move(step));
    }
    return data;
}

void VirtualShadowMapNonRasterableHierarchyPass::Record(const br::render::PreparedComputeDispatchSequence& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatchSequence(data, recording);
}
