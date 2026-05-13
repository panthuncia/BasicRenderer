#include "Managers/Singletons/CommandSignatureManager.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Utilities/Utilities.h"

void CommandSignatureManager::Initialize() {

    auto device = DeviceManager::GetInstance().GetDevice();

    rhi::IndirectArg args[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 3 } } },
        {.kind = rhi::IndirectArgKind::DispatchMesh }
    };
    auto& graphicsLayout = PSOManager::GetInstance().GetRootSignature();
    auto result = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, std::size(args)), sizeof(DispatchMeshIndirectCommand) },
        graphicsLayout.GetHandle(), m_dispatchMeshCommandSignature);

    rhi::IndirectArg args2[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 3 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };
    auto& computeLayout = PSOManager::GetInstance().GetComputeRootSignature();
    result = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args2, std::size(args2)), sizeof(DispatchIndirectCommand) },
        computeLayout.GetHandle(), m_dispatchCommandSignature);

    // Used by the visibility buffer material evaluation pass
    rhi::IndirectArg materialEvaluationArgs[] = {
        {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { IndirectCommandSignatureRootSignatureIndex, 0, 4 } } },
        {.kind = rhi::IndirectArgKind::Dispatch }
    };
    result = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(materialEvaluationArgs, 2), sizeof(MaterialEvaluationIndirectCommand) },
        computeLayout.GetHandle(), m_materialEvaluationCommandSignature);

}

void CommandSignatureManager::Cleanup() {
    m_dispatchMeshCommandSignature.Reset();
    m_dispatchCommandSignature.Reset();
    m_materialEvaluationCommandSignature.Reset();
}
