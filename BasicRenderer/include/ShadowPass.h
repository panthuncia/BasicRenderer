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
		// Setup the render pass
	}

	void Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::getInstance();
		auto& commandList = context.commandList;

		auto shadowRes = getShadowResolution();
		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, shadowRes, shadowRes);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, shadowRes, shadowRes);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto drawObjects = [&]() {
			for (auto& pair : context.currentScene->GetOpaqueRenderableObjectIDMap()) {
				auto& renderable = pair.second;
				auto& meshes = renderable->GetOpaqueMeshes();

				commandList->SetGraphicsRootConstantBufferView(0, renderable->GetConstantBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());

				for (auto& mesh : meshes) {
					auto pso = psoManager.GetPSO(mesh.GetPSOFlags() | PSOFlags::SHADOW, mesh.material->m_blendState);
					commandList->SetPipelineState(pso.Get());
					commandList->SetGraphicsRootConstantBufferView(1, mesh.GetPerMeshBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());
					D3D12_VERTEX_BUFFER_VIEW vertexBufferView = mesh.GetVertexBufferView();
					D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
					commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
					commandList->IASetIndexBuffer(&indexBufferView);

					commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
				}
			}
			for (auto& pair : context.currentScene->GetTransparentRenderableObjectIDMap()) {
				auto& renderable = pair.second;
				auto& meshes = renderable->GetTransparentMeshes();

				commandList->SetGraphicsRootConstantBufferView(0, renderable->GetConstantBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());

				for (auto& mesh : meshes) {
					auto pso = psoManager.GetPSO(mesh.GetPSOFlags() | PSOFlags::SHADOW, mesh.material->m_blendState);
					commandList->SetPipelineState(pso.Get());
					commandList->SetGraphicsRootConstantBufferView(1, mesh.GetPerMeshBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());
					D3D12_VERTEX_BUFFER_VIEW vertexBufferView = mesh.GetVertexBufferView();
					D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
					commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
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
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<uint16_t()> getShadowResolution;
};