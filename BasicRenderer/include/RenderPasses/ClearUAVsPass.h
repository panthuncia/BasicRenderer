#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "DeviceManager.h"
#include "utilities.h"
#include "IndirectCommand.h"

class ClearUAVsPass : public RenderPass {
public:
	ClearUAVsPass() {}

	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();

		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
		m_commandList->Close();
	}

	std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {
		auto commandList = m_commandList.Get();
		ThrowIfFailed(m_commandAllocator->Reset());
		ThrowIfFailed(commandList->Reset(m_commandAllocator.Get(), nullptr));

		auto currentScene = context.currentScene;
		

		auto& objectManager = currentScene->GetObjectManager();
		auto& meshManager = currentScene->GetMeshManager();
		auto counterReset = ResourceManager::GetInstance().GetUAVCounterReset();
		// opaque buffer
		auto resource = currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer()->GetResource();
		auto counterOffset = resource->GetUAVCounterOffset();
		auto apiResource = resource->GetAPIResource();
		
		auto clearBuffer = currentScene->GetIndirectCommandBufferManager()->GetOpaqueClearBuffer();
		auto clearBufferAPIResource = clearBuffer->GetAPIResource();

		//commandList->CopyResource(apiResource, clearBufferAPIResource); // Copy zeroes
		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		for (auto& lightPair : context.currentScene->GetLightIDMap()) {
			auto& light = lightPair.second;
			for (auto& buffer : light->GetPerViewOpaqueIndirectCommandBuffers()) {
				apiResource = buffer->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
			}
		}

		// Alpha test buffer
		resource = currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer()->GetResource();
		counterOffset = resource->GetUAVCounterOffset();
		apiResource = resource->GetAPIResource();

		clearBuffer = currentScene->GetIndirectCommandBufferManager()->GetAlphaTestClearBuffer();
		clearBufferAPIResource = clearBuffer->GetAPIResource();

		//commandList->CopyResource(apiResource, clearBufferAPIResource);
		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		for (auto& lightPair : context.currentScene->GetLightIDMap()) {
			auto& light = lightPair.second;
			for (auto& buffer : light->GetPerViewAlphaTestIndirectCommandBuffers()) {
				apiResource = buffer->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
			}
		}

		// Blende buffer
		resource = currentScene->GetPrimaryCameraBlendIndirectCommandBuffer()->GetResource();
		counterOffset = resource->GetUAVCounterOffset();
		apiResource = resource->GetAPIResource();

		clearBuffer = currentScene->GetIndirectCommandBufferManager()->GetBlendClearBuffer();
		clearBufferAPIResource = clearBuffer->GetAPIResource();

		//commandList->CopyResource(apiResource, clearBufferAPIResource);
		commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));

		for (auto& lightPair : context.currentScene->GetLightIDMap()) {
			auto& light = lightPair.second;
			for (auto& buffer : light->GetPerViewBlendIndirectCommandBuffers()) {
				apiResource = buffer->GetAPIResource();
				commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
			}
		}

		// Close the command list
		ThrowIfFailed(commandList->Close());

		//invalidated = false;

		return { commandList };
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12PipelineState> m_PSO;
};