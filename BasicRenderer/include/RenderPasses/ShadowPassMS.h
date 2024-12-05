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

class ShadowPassMS : public RenderPass {
public:
	ShadowPassMS(std::shared_ptr<ResourceGroup> shadowMaps) {
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("shadowResolution");
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

		auto drawObjects = [&]() {
			unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
			commandList->SetGraphicsRoot32BitConstants(6, 1, &opaquePerMeshBufferIndex, 0);
			for (auto& pair : context.currentScene->GetOpaqueRenderableObjectIDMap()) {
				auto& renderable = pair.second;
				auto& meshes = renderable->GetOpaqueMeshes();

				auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
				commandList->SetGraphicsRoot32BitConstants(0, 1, &perObjectIndex, 0);
				//size_t offset = renderable->GetCurrentPerObjectCBView()->GetOffset();
				//commandList->SetGraphicsRootConstantBufferView(0, objectBufferAddress + offset);

				for (auto& pMesh : meshes) {
					auto& mesh = *pMesh;
					auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW | mesh.material->m_psoFlags, mesh.material->m_blendState);
					commandList->SetPipelineState(pso.Get());
					auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
					commandList->SetGraphicsRoot32BitConstants(1, 1, &perMeshIndex, 0);
					//auto offset = mesh.GetPerMeshBufferView()->GetOffset();
					//commandList->SetGraphicsRootConstantBufferView(1, perMeshBufferAddress + offset);
					commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
				}
			}

			unsigned int transparentPerMeshBufferIndex = meshManager->GetTransparentPerMeshBufferSRVIndex();
			commandList->SetGraphicsRoot32BitConstants(6, 1, &transparentPerMeshBufferIndex, 0);

			for (auto& pair : context.currentScene->GetTransparentRenderableObjectIDMap()) {
				auto& renderable = pair.second;
				auto& meshes = renderable->GetTransparentMeshes();

				auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
				commandList->SetGraphicsRoot32BitConstants(0, 1, &perObjectIndex, 0);
				//size_t offset = renderable->GetCurrentPerObjectCBView()->GetOffset();
				//commandList->SetGraphicsRootConstantBufferView(0, objectBufferAddress + offset);

				for (auto& pMesh : meshes) {
					auto& mesh = *pMesh;
					auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW | mesh.material->m_psoFlags, mesh.material->m_blendState);
					commandList->SetPipelineState(pso.Get());
					auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
					commandList->SetGraphicsRoot32BitConstants(1, 1, &perMeshIndex, 0);
					//auto offset = mesh.GetPerMeshBufferView()->GetOffset();
					//commandList->SetGraphicsRootConstantBufferView(1, perMeshBufferAddress + offset);
					commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
				}
			}
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
					drawObjects();
					break;
				}
				case LightType::Point: {
					int lightIndex = light->GetCurrentLightBufferIndex();
					int lightViewIndex = light->GetCurrentviewInfoIndex()*6;
					commandList->SetGraphicsRoot32BitConstants(2, 1, &lightIndex, 0);
					for (int i = 0; i < 6; i++) {
						D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMap->GetBuffer()->GetDSVInfos()[i].cpuHandle;
						commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
						commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
						commandList->SetGraphicsRoot32BitConstants(3, 1, &lightViewIndex, 0);
						lightViewIndex += 1;
						drawObjects();
					}
					break;
				}
					case LightType::Directional: {
					int lightViewIndex = light->GetCurrentviewInfoIndex()*getNumDirectionalLightCascades();
					int lightIndex = light->GetCurrentLightBufferIndex();
					commandList->SetGraphicsRoot32BitConstants(2, 1, &lightIndex, 0);
					for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
						auto& dsvHandle = shadowMap->GetBuffer()->GetDSVInfos()[i].cpuHandle;
						commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
						commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
						commandList->SetGraphicsRoot32BitConstants(3, 1, &lightViewIndex, 0);
						lightViewIndex += 1;
						drawObjects();

					}
				}
			}
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
	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<uint16_t()> getShadowResolution;
};