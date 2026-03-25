#include "Render/GraphExtensions/ClusterLOD/ReyesQueueResetPass.h"

#include "Managers/Singletons/PSOManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Resources/Buffers/Buffer.h"
#include "../shaders/PerPassRootConstants/clodReyesResetRootConstants.h"

ReyesQueueResetPass::ReyesQueueResetPass(
    std::shared_ptr<Buffer> fullClusterCounter,
    std::vector<std::shared_ptr<Buffer>> splitQueueCounters,
    std::vector<std::shared_ptr<Buffer>> splitQueueOverflowCounters,
    std::shared_ptr<Buffer> diceQueueCounter,
    std::shared_ptr<Buffer> diceQueueOverflowCounter,
    std::shared_ptr<Buffer> ownershipBitsetBuffer,
    std::shared_ptr<Buffer> telemetryBuffer,
    uint32_t phaseIndex)
    : m_fullClusterCounter(std::move(fullClusterCounter))
    , m_splitQueueCounters(std::move(splitQueueCounters))
    , m_splitQueueOverflowCounters(std::move(splitQueueOverflowCounters))
    , m_diceQueueCounter(std::move(diceQueueCounter))
    , m_diceQueueOverflowCounter(std::move(diceQueueOverflowCounter))
    , m_ownershipBitsetBuffer(std::move(ownershipBitsetBuffer))
    , m_telemetryBuffer(std::move(telemetryBuffer))
    , m_phaseIndex(phaseIndex) {
    if (m_ownershipBitsetBuffer) {
        m_ownershipBitsetWordCount = static_cast<uint32_t>(m_ownershipBitsetBuffer->GetSize() / sizeof(uint32_t));
        m_clearOwnershipBitsetPso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/clodUtil.hlsl",
            L"ClearReyesOwnershipBitsetCSMain",
            {},
            "CLod.ReyesQueueReset.ClearOwnershipBitset.PSO");
    }
}

void ReyesQueueResetPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithUnorderedAccess(m_fullClusterCounter, m_diceQueueCounter, m_diceQueueOverflowCounter, m_telemetryBuffer);
    if (m_ownershipBitsetBuffer) {
        builder->WithUnorderedAccess(m_ownershipBitsetBuffer);
    }
    for (const auto& splitQueueCounter : m_splitQueueCounters) {
        builder->WithUnorderedAccess(splitQueueCounter);
    }
    for (const auto& splitQueueOverflowCounter : m_splitQueueOverflowCounters) {
        builder->WithUnorderedAccess(splitQueueOverflowCounter);
    }
}

void ReyesQueueResetPass::Setup() {}

PassReturn ReyesQueueResetPass::Execute(PassExecutionContext& executionContext)
{
    if (!m_ownershipBitsetBuffer || m_ownershipBitsetWordCount == 0u) {
        return {};
    }

    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());
    commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
    commandList.BindPipeline(m_clearOwnershipBitsetPso.GetAPIPipelineState().GetHandle());
    BindResourceDescriptorIndices(commandList, m_clearOwnershipBitsetPso.GetResourceDescriptorSlots());

    uint32_t uintRootConstants[NumMiscUintRootConstants] = {};
    uintRootConstants[CLOD_REYES_RESET_OWNERSHIP_BITSET_DESCRIPTOR_INDEX] = m_ownershipBitsetBuffer->GetUAVShaderVisibleInfo(0).slot.index;
    uintRootConstants[CLOD_REYES_RESET_OWNERSHIP_BITSET_WORD_COUNT] = m_ownershipBitsetWordCount;
    commandList.PushConstants(
        rhi::ShaderStage::Compute,
        0,
        MiscUintRootSignatureIndex,
        0,
        NumMiscUintRootConstants,
        uintRootConstants);

    constexpr uint32_t kThreadsPerGroup = 64u;
    const uint32_t groupCountX = (m_ownershipBitsetWordCount + kThreadsPerGroup - 1u) / kThreadsPerGroup;
    commandList.Dispatch(groupCountX, 1, 1);

    return {};
}

void ReyesQueueResetPass::Update(const UpdateExecutionContext& executionContext)
{
    const uint32_t zero = 0u;
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_fullClusterCounter), 0);
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_diceQueueCounter), 0);
    BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(m_diceQueueOverflowCounter), 0);

    for (const auto& splitQueueCounter : m_splitQueueCounters) {
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(splitQueueCounter), 0);
    }
    for (const auto& splitQueueOverflowCounter : m_splitQueueOverflowCounters) {
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), rg::runtime::UploadTarget::FromShared(splitQueueOverflowCounter), 0);
    }

    CLodReyesTelemetry telemetry{};
    telemetry.phaseIndex = m_phaseIndex;
    telemetry.configuredMaxSplitPassCount = CLodReyesMaxSplitPassCount;
    BUFFER_UPLOAD(&telemetry, sizeof(CLodReyesTelemetry), rg::runtime::UploadTarget::FromShared(m_telemetryBuffer), 0);
}

void ReyesQueueResetPass::Cleanup() {}