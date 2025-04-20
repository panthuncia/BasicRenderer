#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Resources/ResourceHandles.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

class GTAOPass : public ComputePass {
public:
    GTAOPass() {}

    void Setup() override {
        auto& manager = DeviceManager::GetInstance();
        auto& device = manager.GetDevice();
        uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
        for (int i = 0; i < numFramesInFlight; i++) {
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12GraphicsCommandList7> commandList;
            ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator)));
            ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
            commandList->Close();
            m_allocators.push_back(allocator);
            m_commandLists.push_back(commandList);
        }

		CreateXeGTAOComputePSO();
    }

    ComputePassReturn Execute(RenderContext& context) override {
        if (m_texture == nullptr) {
            return { };
        }
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = m_commandLists[context.frameIndex];
        auto& allocator = m_allocators[context.frameIndex];
        ThrowIfFailed(allocator->Reset());
        commandList->Reset(allocator.Get(), nullptr);

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        commandList->Close();

        return { { commandList.Get() } };
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

    void SetTexture(Texture* texture) {
        m_texture = texture;
    }

private:
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    std::shared_ptr<Buffer> vertexBufferHandle;
    Texture* m_texture = nullptr;

    std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
    std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;

    ComPtr<ID3D12RootSignature> gtaoRootSignature;

    ComPtr<ID3D12PipelineState> PrefilterDepths16x16PSO;
    ComPtr<ID3D12PipelineState> GTAOLowPSO;
    ComPtr<ID3D12PipelineState> GTAOMediumPSO;
    ComPtr<ID3D12PipelineState> GTAOHighPSO;
    ComPtr<ID3D12PipelineState> GTAOUltraPSO;
    ComPtr<ID3D12PipelineState> DenoisePassPSO;
    ComPtr<ID3D12PipelineState> DenoiseLastPassPSO;
    ComPtr<ID3D12PipelineState> GenerateNormalsPSO;

    void CreateXeGTAOComputePSO()
    {
        CD3DX12_ROOT_PARAMETER1 params[3];
        params[0].InitAsConstantBufferView(0, 0, 
            D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
            D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_DESCRIPTOR_RANGE1 srvRanges[2];
        srvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, /*baseReg=*/0);  // t0, t1
        srvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, /*baseReg=*/5);  // t5
        params[1].InitAsDescriptorTable(_countof(srvRanges), srvRanges,
            D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_DESCRIPTOR_RANGE1 uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, /*baseReg=*/0);    // u0..u4
        params[2].InitAsDescriptorTable(1, &uavRange,
            D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_STATIC_SAMPLER_DESC pointClamp(
            0,                                // s0
            D3D12_FILTER_MIN_MAG_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
        rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        rsDesc.Desc_1_1.NumParameters = _countof(params);
        rsDesc.Desc_1_1.pParameters   = params;
        rsDesc.Desc_1_1.NumStaticSamplers = 1;
        rsDesc.Desc_1_1.pStaticSamplers   = &pointClamp;

        auto device = DeviceManager::GetInstance().GetDevice();

        ComPtr<ID3DBlob> sigBlob, errorBlob;
        ThrowIfFailed(D3D12SerializeVersionedRootSignature(
            &rsDesc, &sigBlob, &errorBlob));
        ThrowIfFailed(device->CreateRootSignature(
            0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
            IID_PPV_ARGS(&gtaoRootSignature)));

        Microsoft::WRL::ComPtr<ID3DBlob> CSPrefilterDepths16x16;
        PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSPrefilterDepths16x16", L"cs_6_6", {}, CSPrefilterDepths16x16);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = gtaoRootSignature.Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSPrefilterDepths16x16.Get());
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&PrefilterDepths16x16PSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSGTAOLow;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGTAOLow", L"cs_6_6", {}, CSGTAOLow);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGTAOLow.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GTAOLowPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSGTAOMedium;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGTAOMedium", L"cs_6_6", {}, CSGTAOMedium);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGTAOMedium.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GTAOMediumPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSGTAOHigh;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGTAOHigh", L"cs_6_6", {}, CSGTAOHigh);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGTAOHigh.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GTAOHighPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSGTAOUltra;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGTAOUltra", L"cs_6_6", {}, CSGTAOUltra);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGTAOUltra.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GTAOUltraPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSDenoisePass;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSDenoisePass", L"cs_6_6", {}, CSDenoisePass);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSDenoisePass.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&DenoisePassPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSDenoiseLastPass;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSDenoiseLastPass", L"cs_6_6", {}, CSDenoiseLastPass);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSDenoiseLastPass.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&DenoiseLastPassPSO)));
		Microsoft::WRL::ComPtr<ID3DBlob> CSGenerateNormals;
		PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSGenerateNormals", L"cs_6_6", {}, CSGenerateNormals);
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSGenerateNormals.Get());
		ThrowIfFailed(device->CreateComputePipelineState(
			&psoDesc, IID_PPV_ARGS(&GenerateNormalsPSO)));
    }
};
