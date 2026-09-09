#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapClearDirtyBitsPass.h"

#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Texture.h"

#include "../shaders/PerPassRootConstants/clodVirtualShadowClearDirtyBitsRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapClearDirtyBitsPass::VirtualShadowMapClearDirtyBitsPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> allocationRequestsBuffer,
    std::shared_ptr<Buffer> allocationCountBuffer,
    std::shared_ptr<Buffer> indirectArgsBuffer,
    std::shared_ptr<Buffer> dirtyFlagsBuffer,
    std::shared_ptr<Buffer> statsBuffer)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_dirtyFlagsBuffer(std::move(dirtyFlagsBuffer))
    , m_statsBuffer(std::move(statsBuffer))
{
    (void)allocationRequestsBuffer;
    (void)allocationCountBuffer;
    (void)indirectArgsBuffer;

    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowClearDirtyBitsCSMain",
        {},
        "CLod.VirtualShadow.ClearDirtyBits.PSO");
}

void VirtualShadowMapClearDirtyBitsPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithUnorderedAccess(
        m_pageTableTexture,
        m_dirtyFlagsBuffer,
        m_statsBuffer);

    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}

void VirtualShadowMapClearDirtyBitsPass::Initialize()
{
}



br::render::PreparedComputeDispatch VirtualShadowMapClearDirtyBitsPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    auto payload = m_pso.GetPayload(); br::render::PreparedComputeDispatch data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle(); data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle(); auto program = preparation.CaptureProgramBinding(std::move(payload));
    data.program = program.program;
    data.descriptorIndices = std::move(program.descriptorIndices);
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_COMPLETE_EMPTY_ADMITTED_PAGES] = CLodVSMRasterModeUsesLargeClusterPageJob(
        SettingsManager::GetInstance().getSettingGetter<CLodVSMRasterMode>(CLodVSMRasterModeSettingName)()) ? 1u : 0u;
    data.constants[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    data.groupsX = (config.pageTableResolution + 7u) / 8u; data.groupsY = data.groupsX; data.groupsZ = CLodVirtualShadowMaxSupportedClipmapCount;
    return data;
}

void VirtualShadowMapClearDirtyBitsPass::ShutdownPass()
{
}

void VirtualShadowMapClearDirtyBitsPass::Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputeDispatch(data, recording);
}
