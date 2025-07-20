#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"
#include "../shaders/PerPassRootConstants/luminanceHistogramAverageRootConstants.h"

class LuminanceHistogramAveragePass : public ComputePass {
public:
    LuminanceHistogramAveragePass() {}

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithUnorderedAccess(Builtin::PostProcessing::LuminanceHistogram, Builtin::PostProcessing::AdaptedLuminance, "FFX::LPMConstants");
    }

    void Setup() override {
        CreateComputePSO();
        
		RegisterUAV(Builtin::PostProcessing::AdaptedLuminance);
        RegisterUAV(Builtin::PostProcessing::LuminanceHistogram);
		RegisterSRV("FFX::LPMConstants");
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        // Set the compute pipeline state
        commandList->SetComputeRootSignature(psoManager.GetRootSignature().Get());
        commandList->SetPipelineState(pso.Get());

        float passConstants[NumMiscFloatRootConstants] = {};
        passConstants[MIN_LOG_LUMINANCE] = 0.001f; // Minimum log luminance value
        passConstants[LOG_LUMINANCE_RANGE] = (log2(10.0f) - log2(0.1f)); // range for log luminance
        passConstants[TIME_COEFFICIENT] = context.deltaTime;
		passConstants[NUM_PIXELS] = static_cast<float>(context.renderResolution.x * context.renderResolution.y);

        commandList->SetComputeRoot32BitConstants(MiscFloatRootSignatureIndex, NumMiscFloatRootConstants, passConstants, 0);

        BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

		// Dispatch the compute shader
        commandList->Dispatch(1, 1, 1);

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    ComPtr<ID3D12PipelineState> pso;
    PipelineResources m_resourceDescriptorBindings;

    void CreateComputePSO()
    {
        auto device = DeviceManager::GetInstance().GetDevice();

        Microsoft::WRL::ComPtr<ID3DBlob> blob;
        ShaderInfoBundle shaderInfoBundle;
        shaderInfoBundle.computeShader = { L"shaders/PostProcessing/LuminanceHistogramAverage.hlsl", L"CSMain", L"cs_6_6" };
        auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
        blob = compiledBundle.computeShader;
        m_resourceDescriptorBindings = compiledBundle.resourceDescriptorSlots;
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(blob.Get());
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&pso)));
    }
};
