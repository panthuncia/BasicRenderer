#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapInvalidatePagesPass.h"

#include <vector>

#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "BuiltinResources.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/RendererComponents.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/PixelBuffer.h"
#include "Render/GraphExtensions/VirtualShadowCasterProvider.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowInvalidateRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

VirtualShadowMapInvalidatePagesPass::VirtualShadowMapInvalidatePagesPass(
    std::shared_ptr<Buffer> invalidationInputsBuffer,
    std::shared_ptr<Buffer> invalidationCountBuffer,
    std::shared_ptr<Buffer> invalidatedInstancesBitsetBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> directionalPageViewInfoBuffer,
    std::shared_ptr<Buffer> statsBuffer,
    std::shared_ptr<VirtualShadowInvalidationQueue> extensionInvalidations)
    : m_invalidationInputsBuffer(std::move(invalidationInputsBuffer))
    , m_invalidationCountBuffer(std::move(invalidationCountBuffer))
    , m_invalidatedInstancesBitsetBuffer(std::move(invalidatedInstancesBitsetBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_pageTableTexture(std::move(pageTableTexture))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_directionalPageViewInfoBuffer(std::move(directionalPageViewInfoBuffer))
    , m_statsBuffer(std::move(statsBuffer))
    , m_extensionInvalidations(std::move(extensionInvalidations))
{
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowInvalidatePagesCSMain",
        {},
        "CLod.VirtualShadow.InvalidatePages.PSO");
    m_boundsPso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowInvalidateBoundsCSMain",
        {},
        "CLod.VirtualShadow.InvalidateBounds.PSO");
    m_boundsInvalidationBuffer = DynamicBuffer::CreateShared(
        sizeof(float) * 4u + sizeof(uint32_t) * 4u,
        CLodVirtualShadowMaxInvalidationInputs,
        "CLod.VirtualShadow.ExtensionInvalidationBounds");
    auto& ecsWorld = RendererECSManager::GetInstance().GetWorld();

    m_transformChangedQuery = ecsWorld.query_builder<const Components::ObjectDrawInfo>()
        .with<Components::Active>()
        .with<Components::RenderTransformUpdated>()
        .without<Components::SkipShadowPass>()
        .build();
}

void VirtualShadowMapInvalidatePagesPass::Declare(org::PassBuilder& builder)
{
    builder.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
    builder.WithShaderResource(
            Builtin::Shadows::CLodCompactShadowCameras,
            Builtin::PerMeshInstanceBuffer,
            Builtin::InstanceDrawRecordBuffer,
            Builtin::PerInstanceTransformBuffer,
            Builtin::PerObjectBuffer,
            m_invalidationInputsBuffer,
            m_invalidationCountBuffer,
            m_clipmapInfoBuffer,
            m_boundsInvalidationBuffer)
        .WithUnorderedAccess(
            m_pageTableTexture,
            m_dirtyPageFlagsBuffer,
            m_pageMetadataBuffer,
            m_directionalPageViewInfoBuffer,
            m_statsBuffer);

    builder.WithConstantBuffer(Builtin::PerFrameBuffer);
}

void VirtualShadowMapInvalidatePagesPass::Update(const UpdateExecutionContext& executionContext)
{
    (void)executionContext;

    std::vector<CLodVirtualShadowInvalidationInput> inputs;
    inputs.reserve(1024);
    std::vector<uint32_t> invalidatedInstancesBitset(CLodVirtualShadowMovedInstanceBitWordCount(), 0u);

    struct BoundsGpu {
        float centerRadius[4];
        uint32_t clipmapMask;
        uint32_t providerLabel;
        uint32_t pad[2];
    };
    std::vector<BoundsGpu> extensionBounds;
    m_invalidateAllActiveClipmaps = false;
    if (m_extensionInvalidations) {
        auto batch = m_extensionInvalidations->Drain(
            g_clodSkinnedShadowEffectiveDynamicClipmapCount.load(std::memory_order_relaxed));
        m_invalidateAllActiveClipmaps = batch.invalidateAllActiveClipmaps;
        extensionBounds.reserve(batch.bounds.size());
        for (const auto& bounds : batch.bounds) {
            extensionBounds.push_back(BoundsGpu{
                { bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius },
                bounds.clipmapMask,
                bounds.providerLabel,
                { 0u, 0u } });
        }
    }
    m_pendingBoundsCount = static_cast<uint32_t>(extensionBounds.size());
    if (!extensionBounds.empty()) {
        BUFFER_UPLOAD(
            extensionBounds.data(),
            static_cast<uint32_t>(extensionBounds.size() * sizeof(BoundsGpu)),
            org::runtime::UploadTarget::FromShared(m_boundsInvalidationBuffer),
            0);
    }

    auto markInvalidatedInstance = [&invalidatedInstancesBitset](uint32_t drawRecordIndex) {
        if (drawRecordIndex >= CLodVirtualShadowMovedInstanceBitCapacity) {
            return;
        }

        invalidatedInstancesBitset[drawRecordIndex >> 5u] |= 1u << (drawRecordIndex & 31u);
    };

    m_transformChangedQuery.each([&](flecs::entity entity, const Components::ObjectDrawInfo& drawInfo) {
        const bool skinned = entity.has<Components::Skinned>();
        const uint32_t dynamicSkinnedClipmapCount =
            g_clodSkinnedShadowEffectiveDynamicClipmapCount.load(std::memory_order_relaxed);
        const uint32_t clipmapMask = skinned
            ? (dynamicSkinnedClipmapCount >= 32u
                ? 0u
                : (0xFFFFFFFFu << dynamicSkinnedClipmapCount))
            : 0xFFFFFFFFu;
        if (clipmapMask == 0u) {
            return;
        }
        uint32_t flags = 0u;
        flags |= CLodVirtualShadowInvalidationFlagUsePreviousBounds;
        flags |= CLodVirtualShadowInvalidationFlagUseCurrentBounds;

        for (uint32_t drawRecordIndex : drawInfo.instanceDrawRecordIndices) {
            if (inputs.size() >= CLodVirtualShadowMaxInvalidationInputs) {
                break;
            }

            CLodVirtualShadowInvalidationInput input{};
            input.perMeshInstanceBufferIndex = drawRecordIndex;
            input.flags = flags;
            input.clipmapMask = clipmapMask;
            inputs.push_back(input);
            markInvalidatedInstance(drawRecordIndex);
        }
    });

    m_pendingInputCount = static_cast<uint32_t>(inputs.size());
    if (!inputs.empty()) {
        BUFFER_UPLOAD(
            inputs.data(),
            static_cast<uint32_t>(inputs.size() * sizeof(CLodVirtualShadowInvalidationInput)),
            org::runtime::UploadTarget::FromShared(m_invalidationInputsBuffer),
            0);
    }

    BUFFER_UPLOAD(
        &m_pendingInputCount,
        sizeof(m_pendingInputCount),
        org::runtime::UploadTarget::FromShared(m_invalidationCountBuffer),
        0);

    BUFFER_UPLOAD(
        invalidatedInstancesBitset.data(),
        static_cast<uint32_t>(invalidatedInstancesBitset.size() * sizeof(uint32_t)),
        org::runtime::UploadTarget::FromShared(m_invalidatedInstancesBitsetBuffer),
        0);
}

br::render::PreparedComputePipelineSequence VirtualShadowMapInvalidatePagesPass::Prepare(const org::PassPrepareContext& preparation)
{
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    br::render::PreparedComputePipelineSequence data{};
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    std::array<unsigned int, NumMiscUintRootConstants> constants{};
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_INPUTS_DESCRIPTOR_INDEX] = m_invalidationInputsBuffer->GetSRVInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_INPUT_COUNT_DESCRIPTOR_INDEX] = m_invalidationCountBuffer->GetSRVInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_CLIPMAP_INFO_DESCRIPTOR_INDEX] = m_clipmapInfoBuffer->GetSRVInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_PAGE_VIEW_INFO_DESCRIPTOR_INDEX] = m_directionalPageViewInfoBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_BOUNDS_DESCRIPTOR_INDEX] = m_boundsInvalidationBuffer->GetSRVInfo(0).slot.index;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_BOUNDS_COUNT] = m_pendingBoundsCount;
    constants[CLOD_VIRTUAL_SHADOW_INVALIDATE_ALL_ACTIVE_CLIPMAPS] = m_invalidateAllActiveClipmaps ? 1u : 0u;
    const auto append = [&](const PipelineState& pso, uint32_t groups) {
        br::render::PreparedComputePipelineSequence::Step step{};
        auto program = preparation.CaptureProgramBinding(pso);
        step.program = program.program;
        step.descriptorIndices = std::move(program.descriptorIndices);
        step.constants = constants;
        step.groupsX = groups;
        data.steps.push_back(std::move(step));
    };
    if (m_pendingInputCount != 0u) append(m_pso, (m_pendingInputCount + 63u) / 64u);
    if (m_pendingBoundsCount != 0u || m_invalidateAllActiveClipmaps) {
        const uint32_t workCount = m_invalidateAllActiveClipmaps
            ? config.pageTableResolution * config.pageTableResolution * CLodVirtualShadowMaxSupportedClipmapCount
            : m_pendingBoundsCount;
        append(m_boundsPso, (workCount + 63u) / 64u);
    }
    return data;
}

void VirtualShadowMapInvalidatePagesPass::Record(const br::render::PreparedComputePipelineSequence& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedComputePipelineSequence(data, recording);
}
