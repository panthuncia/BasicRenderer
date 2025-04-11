#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "DeviceManager.h"
#include "utilities.h"
#include "IndirectCommand.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "ECSManager.h"
#include "Components.h"

class ClearUAVsPass : public RenderPass {
public:
	ClearUAVsPass() {}

	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();

		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
		m_commandList->Close();

		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::LightViewInfo>().cached().cache_kind(flecs::QueryCacheAll).build();
	}

	RenderPassReturn Execute(RenderContext& context) override {
		auto commandList = m_commandList.Get();
		ThrowIfFailed(m_commandAllocator->Reset());
		ThrowIfFailed(commandList->Reset(m_commandAllocator.Get(), nullptr));

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
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12PipelineState> m_PSO;
};