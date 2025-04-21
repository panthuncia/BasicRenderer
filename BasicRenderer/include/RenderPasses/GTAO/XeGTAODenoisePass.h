#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Resources/ResourceHandles.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

class GTAODenoisePass : public ComputePass {
public:
    GTAODenoisePass(std::shared_ptr<GloballyIndexedResource> pGTAOConstantBuffer) : m_pGTAOConstantBuffer(pGTAOConstantBuffer) {}

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
    std::shared_ptr<GloballyIndexedResource> m_pGTAOConstantBuffer;
    Texture* m_texture = nullptr;
	GTAOInfo m_gtaoInfo;

    std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
    std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;

    ComPtr<ID3D12PipelineState> DenoisePassPSO;
    ComPtr<ID3D12PipelineState> DenoiseLastPassPSO;


    void CreateXeGTAOComputePSO()
    {
        auto device = DeviceManager::GetInstance().GetDevice();

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
       
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
    }
};
