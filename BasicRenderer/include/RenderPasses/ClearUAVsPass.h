#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/ECSManager.h"
#include "Scene/Components.h"

class ClearIndirectDrawCommandUAVsPass : public RenderPass {
public:
	ClearIndirectDrawCommandUAVsPass() {}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::LightViewInfo>().cached().cache_kind(flecs::QueryCacheAll).build();
	}

	PassReturn Execute(RenderContext& context) override {
		// Reset and get the appropriate command list
		auto& commandList = context.commandList;

		auto currentScene = context.currentScene;
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();

		auto& objectManager = *context.objectManager;
		auto& meshManager = *context.meshManager;
		auto counterReset = ResourceManager::GetInstance().GetUAVCounterReset();

		// opaque buffer
		auto resource = currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource();
		auto counterOffset = resource->GetUAVCounterOffset();
		auto apiResource = resource->GetAPIResource();

		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		// Alpha test buffer
		resource = currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource();
		counterOffset = resource->GetUAVCounterOffset();
		apiResource = resource->GetAPIResource();

		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		// Blend buffer
		resource = currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource();
		counterOffset = resource->GetUAVCounterOffset();
		apiResource = resource->GetAPIResource();

		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		lightQuery.each([&](flecs::entity e, Components::LightViewInfo& lightViewInfo) {
			for (auto& view : lightViewInfo.renderViews) {
				auto resource = view.indirectCommandBuffers.opaqueIndirectCommandBuffer->GetResource();
				auto counterOffset = resource->GetUAVCounterOffset();
				auto apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

				resource = view.indirectCommandBuffers.alphaTestIndirectCommandBuffer->GetResource();
				counterOffset = resource->GetUAVCounterOffset();
				apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

				resource = view.indirectCommandBuffers.blendIndirectCommandBuffer->GetResource();
				counterOffset = resource->GetUAVCounterOffset();
				apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
			}
			});
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:
	flecs::query<Components::LightViewInfo> lightQuery;
	ComPtr<ID3D12PipelineState> m_PSO;
};

class ClearMeshletCullingCommandUAVsPass : public RenderPass {
public:
	ClearMeshletCullingCommandUAVsPass() {}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::LightViewInfo>().cached().cache_kind(flecs::QueryCacheAll).build();
	}

	PassReturn Execute(RenderContext& context) override {
		// Reset and get the appropriate command list
		auto& commandList = context.commandList;

		auto currentScene = context.currentScene;
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();

		auto& objectManager = *context.objectManager;
		auto& meshManager = *context.meshManager;
		auto counterReset = ResourceManager::GetInstance().GetUAVCounterReset();

		// Meshlet frustrum culling buffer
		auto resource = currentScene->GetPrimaryCameraMeshletFrustrumCullingIndirectCommandBuffer()->GetResource();
		auto counterOffset = resource->GetUAVCounterOffset();
		auto apiResource = resource->GetAPIResource();
		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		resource = currentScene->GetPrimaryCameraMeshletOcclusionCullingIndirectCommandBuffer()->GetResource();
		counterOffset = resource->GetUAVCounterOffset();
		apiResource = resource->GetAPIResource();
		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		// Meshlet frustrum culling reset buffer
		resource = currentScene->GetPrimaryCameraMeshletCullingResetIndirectCommandBuffer()->GetResource();
		counterOffset = resource->GetUAVCounterOffset();
		apiResource = resource->GetAPIResource();
		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		lightQuery.each([&](flecs::entity e, Components::LightViewInfo& lightViewInfo) {
			for (auto& view : lightViewInfo.renderViews) {
				resource = view.indirectCommandBuffers.meshletFrustrumCullingIndirectCommandBuffer->GetResource();
				counterOffset = resource->GetUAVCounterOffset();
				apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

				resource = view.indirectCommandBuffers.meshletOcclusionCullingIndirectCommandBuffer->GetResource();
				counterOffset = resource->GetUAVCounterOffset();
				apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

				resource = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer->GetResource();
				counterOffset = resource->GetUAVCounterOffset();
				apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
				resource = view.meshletBitfieldBuffer->GetResource();
			}
			});

		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:
	flecs::query<Components::LightViewInfo> lightQuery;
	ComPtr<ID3D12PipelineState> m_PSO;
};