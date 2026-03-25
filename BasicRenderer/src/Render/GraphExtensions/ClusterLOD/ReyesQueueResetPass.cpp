#include "Render/GraphExtensions/ClusterLOD/ReyesQueueResetPass.h"

#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"

ReyesQueueResetPass::ReyesQueueResetPass(
    std::shared_ptr<Buffer> fullClusterCounter,
    std::vector<std::shared_ptr<Buffer>> splitQueueCounters,
    std::vector<std::shared_ptr<Buffer>> splitQueueOverflowCounters,
    std::shared_ptr<Buffer> diceQueueCounter,
    std::shared_ptr<Buffer> diceQueueOverflowCounter,
    std::shared_ptr<Buffer> telemetryBuffer,
    uint32_t phaseIndex)
    : m_fullClusterCounter(std::move(fullClusterCounter))
    , m_splitQueueCounters(std::move(splitQueueCounters))
    , m_splitQueueOverflowCounters(std::move(splitQueueOverflowCounters))
    , m_diceQueueCounter(std::move(diceQueueCounter))
    , m_diceQueueOverflowCounter(std::move(diceQueueOverflowCounter))
    , m_telemetryBuffer(std::move(telemetryBuffer))
    , m_phaseIndex(phaseIndex) {
}

void ReyesQueueResetPass::DeclareResourceUsages(ComputePassBuilder* builder)
{
    builder->WithUnorderedAccess(m_fullClusterCounter, m_diceQueueCounter, m_diceQueueOverflowCounter, m_telemetryBuffer);
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