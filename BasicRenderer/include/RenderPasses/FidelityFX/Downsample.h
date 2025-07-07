#pragma once

#include <flecs.h>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
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

class DownsamplePass : public ComputePass {
public:

    DownsamplePass() {}
    ~DownsamplePass() {
		addObserver.destruct(); // Needed for clean shutdown
		removeObserver.destruct();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithShaderResource(Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }), Subresources(Builtin::Shadows::LinearShadowMaps, Mip{ 0, 1 }))
            .WithUnorderedAccess(Subresources(Builtin::PrimaryCamera::LinearDepthMap, FromMip{ 1 }), Subresources(Builtin::Shadows::LinearShadowMaps, FromMip{ 1 }));
    }

    void Setup() override {
        m_pDownsampleConstants = ResourceManager::GetInstance().CreateIndexedLazyDynamicStructuredBuffer<spdConstants>(1, L"Downsample constants");
		m_pLinearDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::LinearDepthMap);

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
        auto& mapInfo = m_perViewMapInfo[context.currentScene->GetPrimaryCamera().id()];
        if (!mapInfo.pConstantsBufferView) {
			spdlog::error("Downsample pass: No constants buffer view for primary depth map");
        }
        downsampleRootConstants[UintRootConstant0] = mapInfo.pCounterResource->GetUAVShaderVisibleInfo(0).index;
        downsampleRootConstants[UintRootConstant1] = m_pLinearDepthBuffer->GetSRVInfo(0).index;
		downsampleRootConstants[UintRootConstant2] = m_pDownsampleConstants->GetSRVInfo(0).index;
        downsampleRootConstants[UintRootConstant3] = mapInfo.constantsIndex;

        commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, downsampleRootConstants, 0);

		// Dispatch the compute shader for primary depth
		commandList->Dispatch(mapInfo.dispatchThreadGroupCountXY[0], mapInfo.dispatchThreadGroupCountXY[1], 1);

		// Process each shadow map
        lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo, Components::DepthMap shadowMap) {
			auto& mapInfo = m_perViewMapInfo[e.id()];
			downsampleRootConstants[UintRootConstant0] = mapInfo.pCounterResource->GetUAVShaderVisibleInfo(0).index;
            downsampleRootConstants[UintRootConstant1] = light.type == Components::LightType::Point ? shadowMap.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : shadowMap.linearDepthMap->GetSRVInfo(0).index;
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
        uint srcSize[2];
        uint mips;
        uint numWorkGroups;
        
        uint workGroupOffset[2];
        float invInputSize[2];
        
        unsigned int mipUavDescriptorIndices[11];
        uint pad[1];
    };

    struct PerMapInfo {
        unsigned int constantsIndex;
		std::shared_ptr<BufferView> pConstantsBufferView;
		unsigned int dispatchThreadGroupCountXY[2];
        std::shared_ptr<GloballyIndexedResource> pCounterResource;
    };
	std::unordered_map<uint64_t, PerMapInfo> m_perViewMapInfo;

	//spdConstants m_spdConstants;
    //unsigned int m_dispatchThreadGroupCountXY[2];

    unsigned int m_numDirectionalCascades = 0;

    std::shared_ptr<LazyDynamicStructuredBuffer<spdConstants>> m_pDownsampleConstants;
    std::shared_ptr<PixelBuffer> m_pLinearDepthBuffer = nullptr;

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
        //PSOManager::GetInstance().CompileShader(L"shaders/downsample.hlsl", L"DownsampleCSMain", L"cs_6_6", {}, downsample);
		ShaderInfoBundle shaderInfoBundle;
		shaderInfoBundle.computeShader = { L"shaders/downsample.hlsl", L"DownsampleCSMain", L"cs_6_6" };
		auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
		downsample = compiledBundle.computeShader;
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(downsample.Get());
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&downsamplePassPSO)));

        DxcDefine define;
		define.Name = L"DOWNSAMPLE_ARRAY";
		define.Value = L"1";

        Microsoft::WRL::ComPtr<ID3DBlob> downsampleArray;
        //PSOManager::GetInstance().CompileShader(L"shaders/downsample.hlsl", L"DownsampleCSMain", L"cs_6_6", { define }, downsampleArray);
		shaderInfoBundle.computeShader = { L"shaders/downsample.hlsl", L"DownsampleCSMain", L"cs_6_6" };
		shaderInfoBundle.defines = { define };
		compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
		downsampleArray = compiledBundle.computeShader;
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(downsampleArray.Get());
        ThrowIfFailed(device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&downsampleArrayPSO)));
    }

    void AddMapInfo(flecs::entity e, const Components::DepthMap& shadowMap) {

		if (m_perViewMapInfo.contains(e.id())) {
			RemoveMapInfo(e);
		}
		// Hack to allow 2x2 downsampling of arbitrary texture sizes. out-of-bounds loads will be clamped.
        unsigned int paddedWidth = shadowMap.linearDepthMap->GetInternalWidth();
		unsigned int paddedHeight = shadowMap.linearDepthMap->GetInternalHeight();

        auto& shadowMapResource = shadowMap.linearDepthMap;
        unsigned int workGroupOffset[2];
        unsigned int numWorkGroupsAndMips[2];
        unsigned int rectInfo[4];
        rectInfo[0] = 0;
        rectInfo[1] = 0;
        rectInfo[2] = paddedWidth;
        rectInfo[3] = paddedHeight;
        unsigned int threadGroupCountXY[2];
        SpdSetup(threadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

		numWorkGroupsAndMips[1] = shadowMapResource->GetNumUAVMipLevels() - 1; // Mips start at 1, so we subtract 1

        spdConstants spdConstants;
		spdConstants.srcSize[0] = shadowMap.linearDepthMap->GetInternalWidth();
		spdConstants.srcSize[1] = shadowMap.linearDepthMap->GetInternalHeight();
        spdConstants.invInputSize[0] = 1.0f / static_cast<float>(paddedWidth);
        spdConstants.invInputSize[1] = 1.0f / static_cast<float>(paddedHeight);
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
        mapInfo.pCounterResource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int)*6*6, false, true); // 6 ints per slice, up to 6 slices
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
