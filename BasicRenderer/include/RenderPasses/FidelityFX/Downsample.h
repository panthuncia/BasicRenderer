#pragma once

#include <flecs.h>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Resources/ResourceHandles.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"

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
int num = 0;
class DownsamplePass : public ComputePass {
public:

    DownsamplePass() {}
    ~DownsamplePass() {
		addObserver.destruct(); // Needed for clean shutdown
		removeObserver.destruct();
    }
    void Setup() override {
        num++;
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
        
        m_pDownsampleConstants = ResourceManager::GetInstance().CreateIndexedLazyDynamicStructuredBuffer<spdConstants>(1, L"Downsample constants");
        auto primaryViewConstants = m_pDownsampleConstants->Add();
		m_pDownsampleConstants->UpdateView(primaryViewConstants.get(), &m_spdConstants);

		m_pDownsampleAtomicCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int)*6*6, false, true); // 6 ints per slice, up to 6 slices

		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
        lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
		depthQuery = ecsWorld.query_builder<Components::DepthMap>().without<Components::SkipShadowPass>().cached().cache_kind(flecs::QueryCacheAll).build();
        // For each existing depth map, allocate a downsample constant
        depthQuery.each([&](flecs::entity e, Components::DepthMap shadowMap) {
			AddMapInfo(e, shadowMap);
            });

        addObserver = ecsWorld.observer<Components::DepthMap>()
            .event(flecs::OnSet)
            .each([&](flecs::entity e, const Components::DepthMap& p) {
			AddMapInfo(e, p);
                });

        removeObserver = ecsWorld.observer<Components::DepthMap>()
            .event(flecs::OnRemove)
            .each([&](flecs::entity e, const Components::DepthMap& p) {
			RemoveMapInfo(e);
                });

		m_numDirectionalCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")();
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
        // UintRootConstant1 is the index of the source image
        // UintRootConstant2 is the index of the spdConstants structured buffer
		// UintRootConstant3 is the index of the current constants
        unsigned int downsampleRootConstants[NumMiscUintRootConstants] = {};
        downsampleRootConstants[UintRootConstant0] = m_pDownsampleAtomicCounter->GetUAVShaderVisibleInfo(0).index;
        auto& mapInfo = m_perViewMapInfo[context.currentScene->GetPrimaryCamera().id()];
        if (!mapInfo.pConstantsBufferView) {
			spdlog::error("Downsample pass: No constants buffer view for primary depth map");
        }
        downsampleRootConstants[UintRootConstant1] = context.pLinearDepthBuffer->GetSRVInfo(0).index;
		downsampleRootConstants[UintRootConstant2] = m_pDownsampleConstants->GetSRVInfo(0).index;
        downsampleRootConstants[UintRootConstant3] = mapInfo.constantsIndex;

        commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, downsampleRootConstants, 0);

		// Dispatch the compute shader for primary depth
		commandList->Dispatch(m_dispatchThreadGroupCountXY[0], m_dispatchThreadGroupCountXY[1], 1);

		// Process each shadow map
        lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo, Components::DepthMap shadowMap) {
			auto& mapInfo = m_perViewMapInfo[e.id()];
            
            downsampleRootConstants[UintRootConstant1] = shadowMap.linearDepthMap->GetSRVInfo(0).index;
			downsampleRootConstants[UintRootConstant3] = mapInfo.constantsIndex;

			commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, downsampleRootConstants, 0);

            switch (light.type) {
			case Components::LightType::Point:
				commandList->SetPipelineState(downsampleArrayPSO.Get());
				commandList->Dispatch(mapInfo.dispatchThreadGroupCountXY[0], mapInfo.dispatchThreadGroupCountXY[1], 6);
				break;
			case Components::LightType::Spot:
				commandList->SetPipelineState(downsamplePassPSO.Get());
				commandList->Dispatch(mapInfo.dispatchThreadGroupCountXY[0], mapInfo.dispatchThreadGroupCountXY[1], 1);
				break;
			case Components::LightType::Directional:
				commandList->SetPipelineState(downsampleArrayPSO.Get());
                commandList->Dispatch(mapInfo.dispatchThreadGroupCountXY[0], mapInfo.dispatchThreadGroupCountXY[1], m_numDirectionalCascades);
            }

            });

        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup if necessary
    }

private:
    flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;
    flecs::query<Components::DepthMap> depthQuery;

    struct spdConstants
    {
        uint mips;
        uint numWorkGroups;
        uint workGroupOffset[2];
        float invInputSize[2];
        unsigned int mipUavDescriptorIndices[11];
        uint pad[3];
    };

    struct PerMapInfo {
        unsigned int constantsIndex;
		std::shared_ptr<BufferView> pConstantsBufferView;
		unsigned int dispatchThreadGroupCountXY[2];
    };
	std::unordered_map<uint64_t, PerMapInfo> m_perViewMapInfo;

	spdConstants m_spdConstants;
    unsigned int m_dispatchThreadGroupCountXY[2];

    unsigned int m_numDirectionalCascades = 0;

    std::shared_ptr<LazyDynamicStructuredBuffer<spdConstants>> m_pDownsampleConstants;
	std::shared_ptr<GloballyIndexedResource> m_pDownsampleAtomicCounter;

    ComPtr<ID3D12PipelineState> downsamplePassPSO;
	ComPtr<ID3D12PipelineState> downsampleArrayPSO;

    flecs::observer addObserver;
	flecs::observer removeObserver;

    void CreateDownsampleComputePSO()
    {
        auto device = DeviceManager::GetInstance().GetDevice();

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> downsample;
        PSOManager::GetInstance().CompileShader(L"shaders/downsample.hlsl", L"DownsampleCSMain", L"cs_6_6", {}, downsample);
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(downsample.Get());
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&downsamplePassPSO)));

        DxcDefine define;
		define.Name = L"DOWNSAMPLE_ARRAY";
		define.Value = L"1";

        Microsoft::WRL::ComPtr<ID3DBlob> downsampleArray;
        PSOManager::GetInstance().CompileShader(L"shaders/downsample.hlsl", L"DownsampleCSMain", L"cs_6_6", { define }, downsampleArray);
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(downsampleArray.Get());
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&downsampleArrayPSO)));
    }

    void AddMapInfo(flecs::entity e, const Components::DepthMap& shadowMap) {

		if (m_perViewMapInfo.contains(e.id())) {
			RemoveMapInfo(e);
		}

        auto& shadowMapResource = shadowMap.depthMap;
        unsigned int workGroupOffset[2];
        unsigned int numWorkGroupsAndMips[2];
        unsigned int rectInfo[4];
        rectInfo[0] = 0;
        rectInfo[1] = 0;
        rectInfo[2] = shadowMapResource->GetWidth();
        rectInfo[3] = shadowMapResource->GetHeight();
        unsigned int threadGroupCountXY[2];
        SpdSetup(threadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

        spdConstants spdConstants;
        spdConstants.invInputSize[0] = 1.0f / static_cast<float>(shadowMapResource->GetWidth());
        spdConstants.invInputSize[1] = 1.0f / static_cast<float>(shadowMapResource->GetHeight());
        spdConstants.mips = numWorkGroupsAndMips[1];
        spdConstants.numWorkGroups = numWorkGroupsAndMips[0];
        spdConstants.workGroupOffset[0] = workGroupOffset[0];
        spdConstants.workGroupOffset[1] = workGroupOffset[1];

		for (int i = 0; i < (std::min)(11u, shadowMap.linearDepthMap->GetNumUAVMipLevels()-1); i++) {
			spdConstants.mipUavDescriptorIndices[i] = shadowMap.linearDepthMap->GetUAVShaderVisibleInfo(i+1).index;
		}

        auto view = m_pDownsampleConstants->Add();
        m_pDownsampleConstants->UpdateView(view.get(), &spdConstants);

        PerMapInfo mapInfo;
        mapInfo.constantsIndex = view.get()->GetOffset() / sizeof(spdConstants);
        mapInfo.pConstantsBufferView = view;
        mapInfo.dispatchThreadGroupCountXY[0] = threadGroupCountXY[0];
        mapInfo.dispatchThreadGroupCountXY[1] = threadGroupCountXY[1];
        m_perViewMapInfo[e.id()] = mapInfo;
    }

	void RemoveMapInfo(flecs::entity e) {
		auto it = m_perViewMapInfo.find(e.id());
		if (it != m_perViewMapInfo.end()) {
			m_pDownsampleConstants->Remove(it->second.pConstantsBufferView.get());
			m_perViewMapInfo.erase(it);
		}
	}
};
