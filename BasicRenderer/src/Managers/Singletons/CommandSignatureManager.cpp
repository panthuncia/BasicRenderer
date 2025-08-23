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
    rhi::CommandSignatureHandle sig = device.CreateCommandSignature(
        rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(args, 3), sizeof(DispatchMeshIndirectCommand) },
        layout);

    // Dispatch mesh command, for primary draws
    D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[3] = {};
    argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argumentDescs[0].Constant.RootParameterIndex = 0;
    argumentDescs[0].Constant.DestOffsetIn32BitValues = 0;
    argumentDescs[0].Constant.Num32BitValuesToSet = 1;

    argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argumentDescs[1].Constant.RootParameterIndex = 1;
    argumentDescs[1].Constant.DestOffsetIn32BitValues = 0;
    argumentDescs[1].Constant.Num32BitValuesToSet = 2;

    argumentDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
    commandSignatureDesc.pArgumentDescs = argumentDescs;
    commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
    commandSignatureDesc.ByteStride = sizeof(DispatchMeshIndirectCommand);

    auto rootSignature = PSOManager::GetInstance().GetRootSignature();
    ThrowIfFailed(device.CreateCommandSignature(&commandSignatureDesc, rootSignature.Get(), IID_PPV_ARGS(&m_dispatchMeshCommandSignature)));

    // dispatch command for meshlet culling
	D3D12_INDIRECT_ARGUMENT_DESC argumentDescs2[3] = {};
	argumentDescs2[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	argumentDescs2[0].Constant.RootParameterIndex = 0;
	argumentDescs2[0].Constant.DestOffsetIn32BitValues = 0;
	argumentDescs2[0].Constant.Num32BitValuesToSet = 1;

    argumentDescs2[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argumentDescs2[1].Constant.RootParameterIndex = 1;
    argumentDescs2[1].Constant.DestOffsetIn32BitValues = 0;
    argumentDescs2[1].Constant.Num32BitValuesToSet = 2;

    argumentDescs2[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc2 = {};
	commandSignatureDesc2.pArgumentDescs = argumentDescs2;
	commandSignatureDesc2.NumArgumentDescs = _countof(argumentDescs2);
	commandSignatureDesc2.ByteStride = sizeof(DispatchIndirectCommand);

	ThrowIfFailed(device->CreateCommandSignature(&commandSignatureDesc2, rootSignature.Get(), IID_PPV_ARGS(&m_dispatchCommandSignature)));
}

rhi::CommandSignatureHandle& CommandSignatureManager::GetDispatchMeshCommandSignature() {
	return m_dispatchMeshCommandSignature;
}