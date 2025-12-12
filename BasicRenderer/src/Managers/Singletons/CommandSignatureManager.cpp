#include "Managers/Singletons/CommandSignatureManager.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Utilities/Utilities.h"

void CommandSignatureManager::Initialize() {

    auto device = DeviceManager::GetInstance().GetDevice();

    rhi::IndirectArg args[] = {
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { PerObjectRootSignatureIndex, 0, 1 } } },
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { PerMeshRootSignatureIndex, 0, 2 } } },
        { .kind = rhi::IndirectArgKind::DispatchMesh }
    };
	auto& graphicsLayout = PSOManager::GetInstance().GetRootSignature();
    m_dispatchMeshCommandSignature = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 3), sizeof(DispatchMeshIndirectCommand) },
        graphicsLayout.GetHandle());

    rhi::IndirectArg args2[] = {
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { PerObjectRootSignatureIndex, 0, 1 } } },
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { PerMeshRootSignatureIndex, 0, 2 } } },
        { .kind = rhi::IndirectArgKind::Dispatch }
	};
	auto& computeLayout = PSOManager::GetInstance().GetComputeRootSignature();
    m_dispatchCommandSignature = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args2, 3), sizeof(DispatchIndirectCommand) },
        computeLayout.GetHandle());

    rhi::IndirectArg materialEvaluationArgs[] = {
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { MiscUintRootSignatureIndex, 0, 4 } } },
        { .kind = rhi::IndirectArgKind::Dispatch }
	};
    m_materialEvaluationCommandSignature = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(materialEvaluationArgs, 2), sizeof(MaterialEvaluationIndirectCommand) },
		computeLayout.GetHandle());
}
