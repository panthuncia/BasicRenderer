#pragma once

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RenderPasses/Base/ComputePass.h"
#include "BuiltinResources.h"
#include "Managers/ViewManager.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/PixelBuffer.h"
#include "Utilities/Utilities.h"

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

    DownsamplePass()
    {
        CreateDownsampleComputePSO();
        m_pDownsampleConstants = LazyDynamicStructuredBuffer<spdConstants>::CreateShared(1, "Downsample constants");
    }
    ~DownsamplePass() {
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithShaderResource(Subresources(Builtin::PrimaryCamera::LinearDepthMap, Mip{ 0, 1 }), Subresources(Builtin::Shadows::LinearShadowMaps, Mip{ 0, 1 }))
            .WithUnorderedAccess(Subresources(Builtin::PrimaryCamera::LinearDepthMap, FromMip{ 1 }), Subresources(Builtin::Shadows::LinearShadowMaps, FromMip{ 1 }));
    }

    void Setup() override {
    }

    PassReturn Execute(PassExecutionContext& executionContext) override {
        auto* renderContext = executionContext.hostData->Get<RenderContext>();
        auto& context = *renderContext;

        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = executionContext.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        // Set the root signature
        commandList.BindLayout(psoManager.GetRootSignature().GetHandle());

        SyncMapInfos(context);

        for (auto& [resourceID, mapInfo] : m_perMapInfo) {
            if (!mapInfo.pConstantsBufferView || !mapInfo.sourceMap) {
                continue;
            }

            unsigned int downsampleRootConstants[NumMiscUintRootConstants] = {};
            downsampleRootConstants[UintRootConstant0] = mapInfo.pCounterResource->GetUAVShaderVisibleInfo(0).slot.index;
            downsampleRootConstants[UintRootConstant1] = mapInfo.isArrayLike
                ? mapInfo.sourceMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).slot.index
                : mapInfo.sourceMap->GetSRVInfo(0).slot.index;
            downsampleRootConstants[UintRootConstant2] = m_pDownsampleConstants->GetSRVInfo(0).slot.index;
            downsampleRootConstants[UintRootConstant3] = mapInfo.constantsIndex;

            commandList.PushConstants(
                rhi::ShaderStage::Compute,
                0,
                MiscUintRootSignatureIndex,
                0,
                NumMiscUintRootConstants,
                downsampleRootConstants
            );

            commandList.BindPipeline(
                mapInfo.isArrayLike
                    ? downsampleArrayPSO.GetAPIPipelineState().GetHandle()
                    : downsamplePassPSO.GetAPIPipelineState().GetHandle());

            commandList.Dispatch(
                mapInfo.dispatchThreadGroupCountXY[0],
                mapInfo.dispatchThreadGroupCountXY[1],
                mapInfo.dispatchThreadGroupCountZ);
        }

        return {};
    }

    void Cleanup() override {
        // Cleanup if necessary
    }

private:
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
        uint64_t sourceResourceID;
		std::shared_ptr<PixelBuffer> sourceMap;
		bool isArrayLike;
        unsigned int constantsIndex;
		std::shared_ptr<BufferView> pConstantsBufferView;
		unsigned int dispatchThreadGroupCountXY[2];
        unsigned int dispatchThreadGroupCountZ;
        std::shared_ptr<GloballyIndexedResource> pCounterResource;
        uint64_t sourceBackingGeneration;
    };
	std::unordered_map<uint64_t, PerMapInfo> m_perMapInfo;

    std::shared_ptr<LazyDynamicStructuredBuffer<spdConstants>> m_pDownsampleConstants;

    PipelineState downsamplePassPSO;
	PipelineState downsampleArrayPSO;

    static uint32_t GetSliceCount(const PixelBuffer& map)
    {
        const auto& desc = map.GetDescription();
        if (desc.isCubemap) {
            return 6u * (std::max)(1u, desc.arraySize);
        }
        if (desc.isArray) {
            return (std::max)(1u, desc.arraySize);
        }
        return 1u;
    }

    void RemoveMapInfo(uint64_t resourceID) {
		auto it = m_perMapInfo.find(resourceID);
		if (it != m_perMapInfo.end()) {
			m_pDownsampleConstants->Remove(it->second.pConstantsBufferView.get());
			m_perMapInfo.erase(it);
		}
	}

    void CreateOrUpdateMapInfo(uint64_t resourceID, const std::shared_ptr<PixelBuffer>& linearDepthMap) {
        if (!linearDepthMap) {
            return;
        }

        const uint64_t generation = linearDepthMap->GetBackingGeneration();
        auto existing = m_perMapInfo.find(resourceID);
        if (existing != m_perMapInfo.end() && existing->second.sourceBackingGeneration == generation) {
            return;
        }

        if (existing != m_perMapInfo.end()) {
            RemoveMapInfo(resourceID);
        }

		const uint32_t paddedWidth = linearDepthMap->GetInternalWidth();
		const uint32_t paddedHeight = linearDepthMap->GetInternalHeight();

        unsigned int workGroupOffset[2];
        unsigned int numWorkGroupsAndMips[2];
        unsigned int rectInfo[4];
        rectInfo[0] = 0;
        rectInfo[1] = 0;
        rectInfo[2] = paddedWidth;
        rectInfo[3] = paddedHeight;

        unsigned int threadGroupCountXY[2];
        SpdSetup(threadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

		numWorkGroupsAndMips[1] = linearDepthMap->GetNumUAVMipLevels() - 1;

        spdConstants constants = {};
		constants.srcSize[0] = linearDepthMap->GetInternalWidth();
		constants.srcSize[1] = linearDepthMap->GetInternalHeight();
        constants.invInputSize[0] = 1.0f / static_cast<float>(paddedWidth);
        constants.invInputSize[1] = 1.0f / static_cast<float>(paddedHeight);
        constants.mips = numWorkGroupsAndMips[1];
        constants.numWorkGroups = numWorkGroupsAndMips[0];
        constants.workGroupOffset[0] = workGroupOffset[0];
        constants.workGroupOffset[1] = workGroupOffset[1];

		for (unsigned i = 0; i < (std::min)(11u, linearDepthMap->GetNumUAVMipLevels() - 1); i++) {
			constants.mipUavDescriptorIndices[i] = linearDepthMap->GetUAVShaderVisibleInfo(i + 1).slot.index;
		}

        auto constantsView = m_pDownsampleConstants->Add();
        m_pDownsampleConstants->UpdateView(constantsView.get(), &constants);

        PerMapInfo mapInfo = {};
        mapInfo.sourceResourceID = resourceID;
        mapInfo.sourceMap = linearDepthMap;
        mapInfo.isArrayLike = linearDepthMap->GetDescription().isArray || linearDepthMap->GetDescription().isCubemap;
        mapInfo.constantsIndex = static_cast<unsigned int>(constantsView.get()->GetOffset() / sizeof(spdConstants));
        mapInfo.pConstantsBufferView = constantsView;
        mapInfo.dispatchThreadGroupCountXY[0] = threadGroupCountXY[0];
        mapInfo.dispatchThreadGroupCountXY[1] = threadGroupCountXY[1];
        mapInfo.dispatchThreadGroupCountZ = GetSliceCount(*linearDepthMap);
        mapInfo.pCounterResource = CreateIndexedStructuredBuffer(1, sizeof(unsigned int) * 6, true);
        mapInfo.sourceBackingGeneration = generation;

        m_perMapInfo[resourceID] = std::move(mapInfo);
    }

    void SyncMapInfos(const RenderContext& context) {
        std::unordered_map<uint64_t, std::shared_ptr<PixelBuffer>> activeDepthMaps;

        context.viewManager->ForEachView([&](uint64_t viewID) {
            auto* view = context.viewManager->Get(viewID);
            if (!view || !view->gpu.linearDepthMap) {
                return;
            }

            const uint64_t id = view->gpu.linearDepthMap->GetGlobalResourceID();
            activeDepthMaps[id] = view->gpu.linearDepthMap;
        });

        std::vector<uint64_t> stale;
        stale.reserve(m_perMapInfo.size());
        for (const auto& [resourceID, mapInfo] : m_perMapInfo) {
            if (!activeDepthMaps.contains(resourceID)) {
                stale.push_back(resourceID);
            }
        }

        for (uint64_t resourceID : stale) {
            RemoveMapInfo(resourceID);
        }

        for (const auto& [resourceID, depthMap] : activeDepthMaps) {
            CreateOrUpdateMapInfo(resourceID, depthMap);
        }
    }

    void CreateDownsampleComputePSO()
    {
		auto& psoManager = PSOManager::GetInstance();
        auto& layout = psoManager.GetRootSignature();

        // Plain downsample
        downsamplePassPSO = psoManager.MakeComputePipeline(
            layout.GetHandle(),
            L"shaders/downsample.hlsl",
            L"DownsampleCSMain",
            {},                         // no defines
            "DownsampleCS"
        );

        // Array variant (DOWNSAMPLE_ARRAY=1)
        downsampleArrayPSO = psoManager.MakeComputePipeline(
            layout.GetHandle(),
            L"shaders/downsample.hlsl",
            L"DownsampleCSMain",
            { DxcDefine{ L"DOWNSAMPLE_ARRAY", L"1" } },
            "DownsampleCS[Array]"
        );
    }

};
