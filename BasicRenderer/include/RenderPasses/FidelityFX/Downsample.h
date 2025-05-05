#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Resources/ResourceHandles.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"

#define A_CPU
#include "../shaders/FidelityFX/ffx_a.h"
#include "../shaders/FidelityFX/ffx_spd.h"

/*
SpdSetup(
outAU2 dispatchThreadGroupCountXY, // CPU side: dispatch thread group count xy
outAU2 workGroupOffset, // GPU side: pass in as constant
outAU2 numWorkGroupsAndMips, // GPU side: pass in as constant
inAU4 rectInfo, // left, top, width, height
ASU1 mips
*/

class DownsamplePass : public ComputePass {
public:
    DownsamplePass(std::shared_ptr<GloballyIndexedResource> pDownsampleMips, std::shared_ptr<GloballyIndexedResource> pSrcDepths) : m_pDownsampleMips(pDownsampleMips), m_pSrcDepths(pSrcDepths) {}

    void Setup() override {
		DirectX::XMUINT2 screenRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("screenResolution")();
		unsigned int workGroupOffset[2];
		unsigned int numWorkGroupsAndMips[2];
		unsigned int rectInfo[4];
		rectInfo[0] = 0;
		rectInfo[1] = 0;
		rectInfo[2] = screenRes.x;
		rectInfo[3] = screenRes.y;
        SpdSetup(m_dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

		m_spdConstants.invInputSize[0] = 1.0f / static_cast<float>(screenRes.x);
        m_spdConstants.invInputSize[1] = 1.0f / static_cast<float>(screenRes.y);
        m_spdConstants.mips = numWorkGroupsAndMips[1];
        m_spdConstants.numWorkGroups = numWorkGroupsAndMips[0];
        m_spdConstants.workGroupOffset[0] = workGroupOffset[0];
        m_spdConstants.workGroupOffset[1] = workGroupOffset[1];
        
        m_pDownsampleConstants = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(spdConstants));
        UploadManager::GetInstance().UploadData(
            &m_spdConstants,
            sizeof(spdConstants),
            m_pDownsampleConstants.get(),
            0);

		m_pDownsampleAtomicCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int)*6, false, true);

        CreateDownsampleComputePSO();
    }

    PassReturn Execute(RenderContext& context) override {

        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap, // The texture descriptor heap
            context.samplerDescriptorHeap, // The sampler descriptor heap
        };
        commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        // Set the root signature
        commandList->SetComputeRootSignature(psoManager.GetRootSignature().Get());
        commandList->SetPipelineState(downsamplePassPSO.Get());

        // UintRootConstant0 is the index of the global atomic buffer
        // UintRootConstant1 is the index of start of the dst images in the heap
        // UintRootConstant2 is the index of the source image
        // UintRootConstant3 is the index of the single-element spdConstants structured buffer
        unsigned int downsampleRootConstants[NumMiscUintRootConstants] = {};
        downsampleRootConstants[UintRootConstant0] = m_pDownsampleAtomicCounter->GetUAVShaderVisibleInfo()[0].index;
		downsampleRootConstants[UintRootConstant1] = m_pDownsampleMips->GetUAVShaderVisibleInfo()[0].index;
        downsampleRootConstants[UintRootConstant2] = m_pSrcDepths->GetSRVInfo()[0].index;
		downsampleRootConstants[UintRootConstant3] = m_pDownsampleConstants->GetSRVInfo()[0].index;

        commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, downsampleRootConstants, 0);

		// Dispatch the compute shader
		commandList->Dispatch(m_dispatchThreadGroupCountXY[0], m_dispatchThreadGroupCountXY[1], 1);

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    struct spdConstants
    {
        uint mips;
        uint numWorkGroups;
        uint workGroupOffset[2];
        float invInputSize[2];
        unsigned int pad[2];
    };

	spdConstants m_spdConstants;
    unsigned int m_dispatchThreadGroupCountXY[2];

    std::shared_ptr<GloballyIndexedResource> m_pDownsampleMips;
    std::shared_ptr<GloballyIndexedResource> m_pDownsampleConstants;
	std::shared_ptr<GloballyIndexedResource> m_pDownsampleAtomicCounter;

    std::shared_ptr<GloballyIndexedResource> m_pSrcDepths;

    ComPtr<ID3D12PipelineState> downsamplePassPSO;

    void CreateDownsampleComputePSO()
    {
        auto device = DeviceManager::GetInstance().GetDevice();

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> CSDenoisePass;
        PSOManager::GetInstance().CompileShader(L"shaders/downsample.hlsl", L"DownsampleCSMain", L"cs_6_6", {}, CSDenoisePass);
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(CSDenoisePass.Get());
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&downsamplePassPSO)));
    }
};
