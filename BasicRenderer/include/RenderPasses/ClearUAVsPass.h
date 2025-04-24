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

class ClearUAVsPass : public RenderPass {
public:
	ClearUAVsPass() {}

	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();

		uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();

		for (int i = 0; i < numFramesInFlight; i++) {
			ComPtr<ID3D12CommandAllocator> allocator;
			ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));

			ComPtr<ID3D12GraphicsCommandList7> commandList7;
			ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList7)));
			commandList7->Close();
			m_allocators.push_back(allocator);
			m_commandLists.push_back(commandList7);
		}

		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::LightViewInfo>().cached().cache_kind(flecs::QueryCacheAll).build();
	}

	RenderPassReturn Execute(RenderContext& context) override {
		// Reset and get the appropriate command list
		auto& allocator = m_allocators[context.frameIndex];
		allocator->Reset();

		ID3D12GraphicsCommandList* commandList = m_commandLists[context.frameIndex].Get();
		commandList->Reset(allocator.Get(), nullptr);

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
			for (auto& buffer : lightViewInfo.commandBuffers.opaqueIndirectCommandBuffers) {
				auto resource = buffer->GetResource();
				auto counterOffset = resource->GetUAVCounterOffset();
				auto apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
			}
			for (auto& buffer : lightViewInfo.commandBuffers.alphaTestIndirectCommandBuffers) {
				auto resource = buffer->GetResource();
				auto counterOffset = resource->GetUAVCounterOffset();
				auto apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
			}
			for (auto& buffer : lightViewInfo.commandBuffers.blendIndirectCommandBuffers) {
				auto resource = buffer->GetResource();
				auto counterOffset = resource->GetUAVCounterOffset();
				auto apiResource = resource->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
			}
			});

		// Close the command list
		ThrowIfFailed(commandList->Close());

		return { { commandList } };
	}

	void Cleanup(RenderContext& context) override {

	}

private:
	flecs::query<Components::LightViewInfo> lightQuery;
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	ComPtr<ID3D12PipelineState> m_PSO;
};