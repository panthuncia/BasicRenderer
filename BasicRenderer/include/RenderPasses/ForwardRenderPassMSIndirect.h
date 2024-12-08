#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "RenderableObject.h"
#include "mesh.h"
#include "Scene.h"
#include "Material.h"
#include "SettingsManager.h"

class ForwardRenderPassMSIndirect : public RenderPass {
public:
	ForwardRenderPassMSIndirect(bool wireframe) {
		m_wireframe = wireframe;

		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");		
	}
	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();
		uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
		for (int i = 0; i < numFramesInFlight; i++) {
			ComPtr<ID3D12CommandAllocator> allocator;
			ComPtr<ID3D12GraphicsCommandList7> commandList;
			ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
			ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
			commandList->Close();
			m_allocators.push_back(allocator);
			m_commandLists.push_back(commandList);
		}

	}

	std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {
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

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.xRes, context.yRes);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.xRes, context.yRes);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		// Set the render target
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.dsvDescriptorSize);
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto rootSignature = psoManager.GetRootSignature();
		commandList->SetGraphicsRootSignature(rootSignature.Get());

		unsigned int settings[2] = { getShadowsEnabled(), getPunctualLightingEnabled() }; // HLSL bools are 32 bits
		unsigned int punctualLightingEnabled = getPunctualLightingEnabled();
		commandList->SetGraphicsRoot32BitConstants(4, 2, &settings, 0);

		unsigned int staticBufferIndices[6] = {};
		auto& meshManager = context.currentScene->GetMeshManager();
		auto& objectManager = context.currentScene->GetObjectManager();
		auto& cameraManager = context.currentScene->GetCameraManager();
		staticBufferIndices[0] = meshManager->GetVertexBufferIndex();
		staticBufferIndices[1] = meshManager->GetMeshletOffsetBufferIndex();
		staticBufferIndices[2] = meshManager->GetMeshletIndexBufferIndex();
		staticBufferIndices[3] = meshManager->GetMeshletTriangleBufferIndex();
		staticBufferIndices[4] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[5] = cameraManager->GetCameraBufferSRVIndex();

		commandList->SetGraphicsRoot32BitConstants(5, 6, &staticBufferIndices, 0);

		unsigned int localPSOFlags = 0;
		if (getImageBasedLightingEnabled()) {
			localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
		}

		auto commandSignature = context.currentScene->GetIndirectCommandBufferManager()->GetCommandSignature();

		auto numOpaque = context.currentScene->GetNumOpaqueDraws();
		if (numOpaque != 0) {
			unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
			commandList->SetGraphicsRoot32BitConstants(6, 1, &opaquePerMeshBufferIndex, 0);
		
			// Opaque objects
			auto indirectCommandBuffer = context.currentScene->GetPrimaryCameraOpaqueIndirectCommandBuffer();
			auto pso = psoManager.GetMeshPSO(localPSOFlags, BlendState::BLEND_STATE_OPAQUE, m_wireframe);
			commandList->SetPipelineState(pso.Get());
			auto apiResource = indirectCommandBuffer->GetAPIResource();
			commandList->ExecuteIndirect(commandSignature.Get(), numOpaque, apiResource, 0, apiResource, indirectCommandBuffer->GetResource()->GetUAVCounterOffset());
		}

		auto numAlphaTest = context.currentScene->GetNumAlphaTestDraws();
		if (numAlphaTest != 0) {
			unsigned int transparentPerMeshBufferIndex = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
			commandList->SetGraphicsRoot32BitConstants(6, 1, &transparentPerMeshBufferIndex, 0);

			// Transparent objects
			auto indirectCommandBuffer = context.currentScene->GetPrimaryCameraAlphaTestIndirectCommandBuffer();
			auto pso = psoManager.GetMeshPSO(localPSOFlags | PSOFlags::PSO_DOUBLE_SIDED, BlendState::BLEND_STATE_MASK, m_wireframe);
			commandList->SetPipelineState(pso.Get());
			auto apiResource = indirectCommandBuffer->GetAPIResource();
			commandList->ExecuteIndirect(commandSignature.Get(), numAlphaTest, apiResource, 0, apiResource, indirectCommandBuffer->GetResource()->GetUAVCounterOffset());

		}
		commandList->Close();

		return { commandList.Get()};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	bool m_wireframe;
	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;
};