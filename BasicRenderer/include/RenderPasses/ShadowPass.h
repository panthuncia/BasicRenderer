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

class ShadowPass : public RenderPass {
public:
	ShadowPass(std::shared_ptr<ResourceGroup> shadowMaps) {
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

		unsigned int meshletBufferIndices[4] = {};
		auto& meshManager = context.currentScene->GetMeshManager();
		meshletBufferIndices[0] = meshManager->GetVertexBufferIndex();
		meshletBufferIndices[1] = meshManager->GetMeshletOffsetBufferIndex();
		meshletBufferIndices[2] = meshManager->GetMeshletIndexBufferIndex();
		meshletBufferIndices[3] = meshManager->GetMeshletTriangleBufferIndex();
		commandList->SetGraphicsRoot32BitConstants(5, 4, &meshletBufferIndices, 0);

		auto drawObjects = [&]() {
			for (auto& pair : context.currentScene->GetOpaqueRenderableObjectIDMap()) {
				auto& renderable = pair.second;
				auto& meshes = renderable->GetOpaqueMeshes();

				commandList->SetGraphicsRootConstantBufferView(0, renderable->GetConstantBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());

				for (auto& pMesh : meshes) {
					auto& mesh = *pMesh;
					auto pso = psoManager.GetPSO(mesh.GetPSOFlags() | PSOFlags::PSO_SHADOW, mesh.material->m_blendState);
					commandList->SetPipelineState(pso.Get());
					commandList->SetGraphicsRootConstantBufferView(1, mesh.GetPerMeshBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());
					D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
					commandList->IASetIndexBuffer(&indexBufferView);

					commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
				}
			}
			for (auto& pair : context.currentScene->GetTransparentRenderableObjectIDMap()) {
				auto& renderable = pair.second;
				auto& meshes = renderable->GetTransparentMeshes();

				commandList->SetGraphicsRootConstantBufferView(0, renderable->GetConstantBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());

				for (auto& pMesh : meshes) {
					auto& mesh = *pMesh;
					auto pso = psoManager.GetPSO(mesh.GetPSOFlags() | PSOFlags::PSO_SHADOW, mesh.material->m_blendState);
					commandList->SetPipelineState(pso.Get());
					commandList->SetGraphicsRootConstantBufferView(1, mesh.GetPerMeshBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());
					D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
					commandList->IASetIndexBuffer(&indexBufferView);

					commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
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
					auto& dsvHandle = shadowMap->GetHandle().DSVInfo[0].cpuHandle;
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
						D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMap->GetHandle().DSVInfo[i].cpuHandle;
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
						auto& dsvHandle = shadowMap->GetHandle().DSVInfo[i].cpuHandle;
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
		return { commandList };
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12CommandAllocator> m_allocator;
	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<uint16_t()> getShadowResolution;
};