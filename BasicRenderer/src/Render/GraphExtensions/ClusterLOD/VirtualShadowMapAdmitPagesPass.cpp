#include "Render/GraphExtensions/ClusterLOD/VirtualShadowMapAdmitPagesPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "BuiltinResources.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include <tracy/Tracy.hpp>

#include "../shaders/PerPassRootConstants/clodVirtualShadowAdmitPagesRootConstants.h"
#include "../shaders/PerPassRootConstants/clodVirtualShadowApplyUpgradesRootConstants.h"

VirtualShadowMapAdmitPagesPass::VirtualShadowMapAdmitPagesPass(
    std::shared_ptr<PixelBuffer> pageTableTexture,
    std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
    std::vector<std::shared_ptr<Buffer>> upgradeInputBuffers,
    std::shared_ptr<Buffer> pageMetadataBuffer,
    std::shared_ptr<Buffer> clipmapInfoBuffer,
    std::shared_ptr<Buffer> compactShadowCamerasBuffer,
    std::shared_ptr<Buffer> statsBuffer,
    AcquireUpgradeUploadFn acquireUpgradeUpload,
    ReleaseUpgradeUploadFn releaseUpgradeUpload,
    uint32_t framesInFlight)
    : m_pageTableTexture(std::move(pageTableTexture))
    , m_dirtyPageFlagsBuffer(std::move(dirtyPageFlagsBuffer))
    , m_upgradeInputBuffers(std::move(upgradeInputBuffers))
    , m_pageMetadataBuffer(std::move(pageMetadataBuffer))
    , m_clipmapInfoBuffer(std::move(clipmapInfoBuffer))
    , m_compactShadowCamerasBuffer(std::move(compactShadowCamerasBuffer))
    , m_statsBuffer(std::move(statsBuffer))
    , m_upgradeState(std::make_shared<UpgradeSubmissionState>())
{
    m_upgradeState->acquire = std::move(acquireUpgradeUpload);
    m_upgradeState->release = std::move(releaseUpgradeUpload);
    m_upgradeState->inFlightSlotByFrame.assign(std::max(framesInFlight, 1u), UINT32_MAX);
    m_pso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowAdmitPagesCSMain",
        {},
        "CLod.VirtualShadow.AdmitPages.PSO");
    m_applyUpgradesPso = PSOManager::GetInstance().MakeComputePipeline(
        PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
        L"Shaders/ClusterLOD/clodUtil.hlsl",
        L"CLodVirtualShadowApplyExactUpgradesCSMain",
        {},
        "CLod.VirtualShadow.ApplyUpgrades.PSO");
}

void VirtualShadowMapAdmitPagesPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithShaderResource(
            m_clipmapInfoBuffer,
            m_compactShadowCamerasBuffer)
        .WithUnorderedAccess(
            m_pageTableTexture,
            m_dirtyPageFlagsBuffer,
            m_pageMetadataBuffer,
            m_statsBuffer);
    for (const auto& buffer : m_upgradeInputBuffers) {
        builder->WithShaderResource(buffer);
    }
}

void VirtualShadowMapAdmitPagesPass::Setup() {}

void VirtualShadowMapAdmitPagesPass::Update(
    const UpdateExecutionContext& executionContext)
{
    ZoneScopedN("VirtualShadowMapAdmitPagesPass::Update");
    std::scoped_lock lock(m_upgradeState->mutex);
    const uint32_t frameSlot = executionContext.frameIndex %
        static_cast<uint32_t>(m_upgradeState->inFlightSlotByFrame.size());
    if (m_upgradeState->inFlightSlotByFrame[frameSlot] != UINT32_MAX) {
        m_upgradeState->release(m_upgradeState->inFlightSlotByFrame[frameSlot]);
        m_upgradeState->inFlightSlotByFrame[frameSlot] = UINT32_MAX;
    }
    if (m_upgradeState->pendingSlot == UINT32_MAX && m_upgradeState->acquire) {
        uint32_t slotIndex = UINT32_MAX;
        uint32_t inputCount = 0u;
        if (m_upgradeState->acquire(slotIndex, inputCount)) {
            m_upgradeState->pendingSlot = slotIndex;
            m_upgradeState->pendingCount = inputCount;
            ++m_upgradeState->pendingGeneration;
        }
    }
}

PassReturn VirtualShadowMapAdmitPagesPass::Execute(PassExecutionContext& executionContext)
{
    uint32_t pendingUpgradeSlot = UINT32_MAX;
    uint32_t pendingUpgradeInputCount = 0;
    uint64_t pendingGeneration = 0;
    {
        std::scoped_lock lock(m_upgradeState->mutex);
        pendingUpgradeSlot = m_upgradeState->pendingSlot;
        pendingUpgradeInputCount = m_upgradeState->pendingCount;
        pendingGeneration = m_upgradeState->pendingGeneration;
    }
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& commandList = executionContext.commandList;
    commandList.SetDescriptorHeaps(
        renderContext->textureDescriptorHeap.GetHandle(),
        renderContext->samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    const uint32_t normalBudget =
        SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodDirectionalVirtualShadowPageRenderBudgetSettingName)();
    const uint32_t upgradeBudget =
        SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodDirectionalVirtualShadowUpgradePageRenderBudgetSettingName)();
    constexpr uint32_t threadsPerGroup = 64u;
    const uint32_t pageCount = config.pageTableResolution * config.pageTableResolution;

    rhi::GlobalBarrier globalBarrier{};
    globalBarrier.beforeSync = rhi::ResourceSyncState::ComputeShading;
    globalBarrier.afterSync = rhi::ResourceSyncState::ComputeShading;
    globalBarrier.beforeAccess = rhi::ResourceAccessType::UnorderedAccess;
    globalBarrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
    rhi::BarrierBatch barrierBatch{};
    barrierBatch.globals = rhi::Span<rhi::GlobalBarrier>(&globalBarrier, 1);

    if (pendingUpgradeSlot < m_upgradeInputBuffers.size() &&
        pendingUpgradeInputCount != 0u) {
        commandList.BindPipeline(
            m_applyUpgradesPso.GetAPIPipelineState().GetHandle());
        BindResourceDescriptorIndices(
            commandList,
            m_applyUpgradesPso.GetResourceDescriptorSlots());
        uint32_t rootConstants[NumMiscUintRootConstants] = {};
        rootConstants[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_INPUTS_DESCRIPTOR_INDEX] =
            m_upgradeInputBuffers[pendingUpgradeSlot]->GetSRVInfo(0).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_INPUT_COUNT] =
            pendingUpgradeInputCount;
        rootConstants[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_PAGE_TABLE_DESCRIPTOR_INDEX] =
            m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_DIRTY_FLAGS_DESCRIPTOR_INDEX] =
            m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_STATS_DESCRIPTOR_INDEX] =
            m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_PAGE_METADATA_DESCRIPTOR_INDEX] =
            m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        rootConstants[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_CLIPMAP_COUNT] =
            CLodVirtualShadowMaxSupportedClipmapCount;
        commandList.PushConstants(
            rhi::ShaderStage::Compute,
            0,
            MiscUintRootSignatureIndex,
            0,
            NumMiscUintRootConstants,
            rootConstants);
        commandList.Dispatch(
            (pendingUpgradeInputCount + threadsPerGroup - 1u) /
                threadsPerGroup,
            1u,
            1u);
        commandList.Barriers(barrierBatch);
        const uint32_t frameSlot = executionContext.frameIndex %
            static_cast<uint32_t>(m_upgradeState->inFlightSlotByFrame.size());
        std::scoped_lock lock(m_upgradeState->mutex);
        if (m_upgradeState->pendingGeneration == pendingGeneration &&
            m_upgradeState->pendingSlot == pendingUpgradeSlot) {
            m_upgradeState->inFlightSlotByFrame[frameSlot] = pendingUpgradeSlot;
            m_upgradeState->pendingSlot = UINT32_MAX;
            m_upgradeState->pendingCount = 0u;
        }
    }

    commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

    // Upgrade work receives its reserved share first. Dispatching clipmaps
    // individually makes near-clipmap priority deterministic across classes.
    for (uint32_t phaseIteration = 0u; phaseIteration < 2u; ++phaseIteration) {
        const uint32_t upgradePhase = 1u - phaseIteration;
        for (uint32_t clipmapIndex = 0u; clipmapIndex < CLodVirtualShadowMaxSupportedClipmapCount; ++clipmapIndex) {
            uint32_t rootConstants[NumMiscUintRootConstants] = {};
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_PAGE_TABLE_DESCRIPTOR_INDEX] =
                m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_DIRTY_FLAGS_DESCRIPTOR_INDEX] =
                m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_STATS_DESCRIPTOR_INDEX] =
                m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_CLIPMAP_INDEX] = clipmapIndex;
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_NORMAL_BUDGET] = normalBudget;
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_UPGRADE_BUDGET] = upgradeBudget;
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_UPGRADE_PHASE] = upgradePhase;
            rootConstants[CLOD_VIRTUAL_SHADOW_ADMIT_APPLY_UPGRADES_ONLY] = 0u;
            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                rootConstants);
            commandList.Dispatch((pageCount + threadsPerGroup - 1u) / threadsPerGroup, 1u, 1u);
            commandList.Barriers(barrierBatch);
        }
    }
    return {};
}

PreparedPass VirtualShadowMapAdmitPagesPass::PrepareFrame(FramePreparationContext& preparation)
{
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    PreparedData data{};
    data.commands.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.commands.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    data.commands.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
    data.state = m_upgradeState;
    data.frameSlot = preparation.frameIndex % static_cast<uint32_t>(m_upgradeState->inFlightSlotByFrame.size());
    {
        std::scoped_lock lock(m_upgradeState->mutex);
        data.pendingSlot = m_upgradeState->pendingSlot;
        data.pendingCount = m_upgradeState->pendingCount;
        data.pendingGeneration = m_upgradeState->pendingGeneration;
    }
    const auto config = CLodVirtualShadowBuildRuntimeResolutionConfig();
    const uint32_t normalBudget = SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodDirectionalVirtualShadowPageRenderBudgetSettingName)();
    const uint32_t upgradeBudget = SettingsManager::GetInstance().getSettingGetter<uint32_t>(CLodDirectionalVirtualShadowUpgradePageRenderBudgetSettingName)();
    const auto append = [&](const PipelineState& pso, std::array<unsigned int, NumMiscUintRootConstants> constants,
        uint32_t groups, bool barrierAfter) {
        br::render::PreparedComputePipelineSequence::Step step{};
        step.pipelineOwner = pso.GetPayload();
        step.pipeline = step.pipelineOwner->pso.Get().GetHandle();
        step.descriptorIndices = CaptureResourceDescriptorIndices(step.pipelineOwner->pipelineResources);
        step.constants = constants;
        step.groupsX = groups;
        step.uavBarrierAfter = barrierAfter;
        data.commands.steps.push_back(std::move(step));
    };
    if (data.pendingSlot < m_upgradeInputBuffers.size() && data.pendingCount != 0u) {
        std::array<unsigned int, NumMiscUintRootConstants> c{};
        c[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_INPUTS_DESCRIPTOR_INDEX] = m_upgradeInputBuffers[data.pendingSlot]->GetSRVInfo(0).slot.index;
        c[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_INPUT_COUNT] = data.pendingCount;
        c[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
        c[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_PAGE_METADATA_DESCRIPTOR_INDEX] = m_pageMetadataBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        c[CLOD_VIRTUAL_SHADOW_APPLY_UPGRADES_CLIPMAP_COUNT] = CLodVirtualShadowMaxSupportedClipmapCount;
        append(m_applyUpgradesPso, c, (data.pendingCount + 63u) / 64u, true);
    }
    const uint32_t pageGroups = (config.pageTableResolution * config.pageTableResolution + 63u) / 64u;
    for (uint32_t phaseIteration = 0; phaseIteration < 2; ++phaseIteration) {
        for (uint32_t clipmapIndex = 0; clipmapIndex < CLodVirtualShadowMaxSupportedClipmapCount; ++clipmapIndex) {
            std::array<unsigned int, NumMiscUintRootConstants> c{};
            c[CLOD_VIRTUAL_SHADOW_ADMIT_PAGE_TABLE_DESCRIPTOR_INDEX] = m_pageTableTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index;
            c[CLOD_VIRTUAL_SHADOW_ADMIT_DIRTY_FLAGS_DESCRIPTOR_INDEX] = m_dirtyPageFlagsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            c[CLOD_VIRTUAL_SHADOW_ADMIT_STATS_DESCRIPTOR_INDEX] = m_statsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
            c[CLOD_VIRTUAL_SHADOW_ADMIT_PAGE_TABLE_RESOLUTION] = config.pageTableResolution;
            c[CLOD_VIRTUAL_SHADOW_ADMIT_CLIPMAP_INDEX] = clipmapIndex;
            c[CLOD_VIRTUAL_SHADOW_ADMIT_NORMAL_BUDGET] = normalBudget;
            c[CLOD_VIRTUAL_SHADOW_ADMIT_UPGRADE_BUDGET] = upgradeBudget;
            c[CLOD_VIRTUAL_SHADOW_ADMIT_UPGRADE_PHASE] = 1u - phaseIteration;
            append(m_pso, c, pageGroups, true);
        }
    }
    return PreparedPass::Make(std::move(data), &RecordPrepared, &CommitPrepared);
}

void VirtualShadowMapAdmitPagesPass::RecordPrepared(const PreparedData& data, RecordingContext& recording)
{
    br::render::RecordPreparedComputePipelineSequence(data.commands, recording);
}

void VirtualShadowMapAdmitPagesPass::CommitPrepared(const PreparedData& data)
{
    if (data.pendingSlot == UINT32_MAX || data.pendingCount == 0) return;
    std::scoped_lock lock(data.state->mutex);
    if (data.state->pendingGeneration != data.pendingGeneration || data.state->pendingSlot != data.pendingSlot) return;
    data.state->inFlightSlotByFrame.at(data.frameSlot) = data.pendingSlot;
    data.state->pendingSlot = UINT32_MAX;
    data.state->pendingCount = 0;
}

void VirtualShadowMapAdmitPagesPass::Cleanup() {}
