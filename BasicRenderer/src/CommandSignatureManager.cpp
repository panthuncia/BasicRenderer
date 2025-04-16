#include "CommandSignatureManager.h"
#include "IndirectCommand.h"
#include "Managers/Singletons/DeviceManager.h"
#include "PSOManager.h"
#include "utilities.h"

void CommandSignatureManager::Initialize() {
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

    auto device = DeviceManager::GetInstance().GetDevice();
    auto rootSignature = PSOManager::GetInstance().GetRootSignature();
    ThrowIfFailed(device->CreateCommandSignature(&commandSignatureDesc, rootSignature.Get(), IID_PPV_ARGS(&m_dispatchMeshCommandSignature)));
}

ID3D12CommandSignature* CommandSignatureManager::GetDispatchMeshCommandSignature() {
	return m_dispatchMeshCommandSignature.Get();
}