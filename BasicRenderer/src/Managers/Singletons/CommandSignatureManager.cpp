#include "Managers/Singletons/CommandSignatureManager.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Utilities/Utilities.h"

void CommandSignatureManager::Initialize() {

    auto device = DeviceManager::GetInstance().GetDevice();

    rhi::IndirectArg args[] = {
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { 0, 0, 1 } } },
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { 1, 0, 2 } } },
        { .kind = rhi::IndirectArgKind::DispatchMesh }
    };
	auto& layout = PSOManager::GetInstance().GetRootSignature();
    m_dispatchMeshCommandSignature = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 3), sizeof(DispatchMeshIndirectCommand) },
        layout.GetHandle());

    rhi::IndirectArg args2[] = {
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { 0, 0, 1 } } },
        { .kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { 1, 0, 2 } } },
        { .kind = rhi::IndirectArgKind::Dispatch }
	};
    m_dispatchCommandSignature = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args2, 3), sizeof(DispatchIndirectCommand) },
		layout.GetHandle());
}

rhi::CommandSignature& CommandSignatureManager::GetDispatchMeshCommandSignature() {
	return m_dispatchMeshCommandSignature.Get();
}

rhi::CommandSignature& CommandSignatureManager::GetDispatchCommandSignature() {
    return m_dispatchCommandSignature.Get();
}

