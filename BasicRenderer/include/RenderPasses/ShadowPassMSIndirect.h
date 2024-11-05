#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "RenderableObject.h"
#include "mesh.h"
#include "Scene.h"
#include "ResourceGroup.h"
#include "SettingsManager.h"

class ShadowPassMSIndirect : public RenderPass {
public:
	ShadowPassMSIndirect(std::shared_ptr<ResourceGroup> shadowMaps) {
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("shadowResolution");
	}
	void Setup() override {
		auto& manager = DeviceManager::GetInstance();
		auto& device = manager.GetDevice();
		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
		m_commandList->Close();
	}

	std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::getInstance();
		auto commandList = m_commandList.Get();
		ThrowIfFailed(m_allocator->Reset());
		commandList->Reset(m_allocator.Get(), nullptr);

		ID3D12DescriptorHeap* descriptorHeaps[] = {
			context.textureDescriptorHeap, // The texture descriptor heap
			context.samplerDescriptorHeap, // The sampler descriptor heap
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		auto shadowRes = getShadowResolution();
		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, shadowRes, shadowRes);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, shadowRes, shadowRes);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		commandList->SetGraphicsRootSignature(psoManager.GetRootSignature().Get());

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

		//D3D12_GPU_VIRTUAL_ADDRESS objectBufferAddress = context.currentScene->GetObjectManager()->GetPerObjectBuffers()->GetBuffer()->m_buffer->GetGPUVirtualAddress();
		//D3D12_GPU_VIRTUAL_ADDRESS perMeshBufferAddress = context.currentScene->GetMeshManager()->GetPerMeshBuffers()->GetBuffer()->m_buffer->GetGPUVirtualAddress();

		auto commandSignature = context.currentScene->GetIndirectCommandBufferManager()->GetCommandSignature();

		auto drawObjects = [&](ID3D12Resource* opaqueIndirectCommandBuffer, ID3D12Resource* transparentIndirectCommandBuffer) {
			unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
			commandList->SetGraphicsRoot32BitConstants(6, 1, &opaquePerMeshBufferIndex, 0);

			auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW, BlendState::BLEND_STATE_OPAQUE, false);
			commandList->SetPipelineState(pso.Get());
			commandList->ExecuteIndirect(commandSignature.Get(), context.currentScene->GetNumOpaqueDraws(), opaqueIndirectCommandBuffer, 0, nullptr, 0);

			unsigned int transparentPerMeshBufferIndex = meshManager->GetTransparentPerMeshBufferSRVIndex();
			commandList->SetGraphicsRoot32BitConstants(6, 1, &transparentPerMeshBufferIndex, 0);

			pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW, BlendState::BLEND_STATE_OPAQUE, false);
			commandList->SetPipelineState(pso.Get());
			commandList->ExecuteIndirect(commandSignature.Get(), context.currentScene->GetNumTransparentDraws(), transparentIndirectCommandBuffer, 0, nullptr, 0);
			
		};

		for (auto& lightPair : context.currentScene->GetLightIDMap()) {
			auto& light = lightPair.second;
			auto& shadowMap = light->getShadowMap();
			if (!shadowMap) {
				continue;
			}
			float clear[4] = { 1.0, 0.0, 0.0, 0.0 };
			switch (light->GetLightType()) {
				case LightType::Spot: {
					auto& dsvHandle = shadowMap->GetBuffer()->GetDSVInfos()[0].cpuHandle;
					commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
					commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
					int lightIndex = light->GetCurrentLightBufferIndex();
					commandList->SetGraphicsRoot32BitConstants(2, 1, &lightIndex, 0);
					int lightViewIndex = light->GetCurrentviewInfoIndex();
					commandList->SetGraphicsRoot32BitConstants(3, 1, &lightViewIndex, 0);

					drawObjects(light->GetPerViewOpaqueIndirectCommandBuffers()[0]->GetAPIResource(), light->GetPerViewTransparentIndirectCommandBuffers()[0]->GetAPIResource());
					break;
				}
				case LightType::Point: {
					int lightIndex = light->GetCurrentLightBufferIndex();
					int lightViewIndex = light->GetCurrentviewInfoIndex()*6;
					commandList->SetGraphicsRoot32BitConstants(2, 1, &lightIndex, 0);
					auto& opaqueBuffers = light->GetPerViewOpaqueIndirectCommandBuffers();
					auto& transparentBuffers = light->GetPerViewTransparentIndirectCommandBuffers();
					for (int i = 0; i < 6; i++) {
						D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMap->GetBuffer()->GetDSVInfos()[i].cpuHandle;
						commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
						commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
						commandList->SetGraphicsRoot32BitConstants(3, 1, &lightViewIndex, 0);
						lightViewIndex += 1;
						drawObjects(opaqueBuffers[i]->GetAPIResource(), transparentBuffers[i]->GetAPIResource());
					}
					break;
				}
					case LightType::Directional: {
					int lightViewIndex = light->GetCurrentviewInfoIndex()*getNumDirectionalLightCascades();
					int lightIndex = light->GetCurrentLightBufferIndex();
					commandList->SetGraphicsRoot32BitConstants(2, 1, &lightIndex, 0);
					auto& opaqueBuffers = light->GetPerViewOpaqueIndirectCommandBuffers();
					auto& transparentBuffers = light->GetPerViewTransparentIndirectCommandBuffers();
					for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
						auto& dsvHandle = shadowMap->GetBuffer()->GetDSVInfos()[i].cpuHandle;
						commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
						commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
						commandList->SetGraphicsRoot32BitConstants(3, 1, &lightViewIndex, 0);
						lightViewIndex += 1;
						drawObjects(opaqueBuffers[i]->GetAPIResource(), transparentBuffers[i]->GetAPIResource());
					}
				}
			}
		}
		commandList->Close();
		return { commandList };
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	ComPtr<ID3D12GraphicsCommandList7> m_commandList;
	ComPtr<ID3D12CommandAllocator> m_allocator;
	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<uint16_t()> getShadowResolution;
};