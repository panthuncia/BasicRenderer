#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Resources/ResourceHandles.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

class GTAOFilterPass : public ComputePass {
public:
    GTAOFilterPass(std::shared_ptr<GloballyIndexedResource> pGTAOConstantBuffer) : m_pGTAOConstantBuffer(pGTAOConstantBuffer) {}

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

		// Set the compute pipeline state
		commandList->SetComputeRootSignature(psoManager.GetRootSignature().Get());
		commandList->SetPipelineState(PrefilterDepths16x16PSO.Get());

        unsigned int passConstants[NumMiscRootConstants] = {};
        passConstants[0] = m_pGTAOConstantBuffer->GetCBVInfo().index;


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

    ComPtr<ID3D12PipelineState> PrefilterDepths16x16PSO;

    void CreateXeGTAOComputePSO()
    {
        auto device = DeviceManager::GetInstance().GetDevice();

        Microsoft::WRL::ComPtr<ID3DBlob> CSPrefilterDepths16x16;
        PSOManager::GetInstance().CompileShader(L"shaders/GTAO.hlsl", L"CSPrefilterDepths16x16", L"cs_6_6", {}, CSPrefilterDepths16x16);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSPrefilterDepths16x16.Get());
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&PrefilterDepths16x16PSO)));
    }
};
