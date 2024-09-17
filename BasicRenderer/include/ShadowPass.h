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
	}
	void Setup() override {
		// Setup the render pass
	}

	void Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::getInstance();
		auto& commandList = context.commandList;

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.xRes, context.yRes);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.xRes, context.yRes);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		// Set the render target
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart());
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		// Clear the render target
		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto drawObjects = [&]() {
			for (auto& pair : context.currentScene->GetOpaqueRenderableObjectIDMap()) {
				auto& renderable = pair.second;
				auto& meshes = renderable->GetOpaqueMeshes();

				commandList->SetGraphicsRootConstantBufferView(1, renderable->GetConstantBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());

				for (auto& mesh : meshes) {
					auto pso = psoManager.GetPSO(mesh.GetPSOFlags() | PSOFlags::SHADOW, mesh.material->m_blendState);
					commandList->SetPipelineState(pso.Get());
					commandList->SetGraphicsRootConstantBufferView(2, mesh.GetPerMeshBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());
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

				commandList->SetGraphicsRootConstantBufferView(1, renderable->GetConstantBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());

				for (auto& mesh : meshes) {
					auto pso = psoManager.GetPSO(mesh.GetPSOFlags() | PSOFlags::SHADOW, mesh.material->m_blendState);
					commandList->SetPipelineState(pso.Get());
					commandList->SetGraphicsRootConstantBufferView(2, mesh.GetPerMeshBuffer().dataBuffer->m_buffer->GetGPUVirtualAddress());
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
			switch (light->GetLightType()) {
				case LightType::Spot: {
					commandList->OMSetRenderTargets(1, nullptr, TRUE, &shadowMap->GetHandle().DSVInfo[0].cpuHandle);
					int lightIndex = light->GetCurrentLightBufferIndex();
					commandList->SetGraphicsRoot32BitConstants(3, 1, &lightIndex, 0);
					int lightViewIndex = light->GetCurrentviewInfoIndex();
					commandList->SetGraphicsRoot32BitConstants(4, 1, &lightViewIndex, 0);
					drawObjects();
					break;
				}
				case LightType::Point: {
					int lightIndex = light->GetCurrentLightBufferIndex();
					int lightViewIndex = light->GetCurrentviewInfoIndex();
					for (int i = 0; i < 6; i++) {
						commandList->OMSetRenderTargets(0, nullptr, TRUE, &shadowMap->GetHandle().DSVInfo[i].cpuHandle);
						commandList->SetGraphicsRoot32BitConstants(3, 1, &lightIndex, 0);
						lightViewIndex += 1;
						commandList->SetGraphicsRoot32BitConstants(4, 1, &lightViewIndex, 0);
						drawObjects();
					}
					break;
				}
					case LightType::Directional: {
					int lightViewIndex = light->GetCurrentviewInfoIndex();
					int lightIndex = light->GetCurrentLightBufferIndex();
					for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
						commandList->OMSetRenderTargets(0, nullptr, TRUE, &shadowMap->GetHandle().DSVInfo[i].cpuHandle);
						commandList->SetGraphicsRoot32BitConstants(3, 1, &lightIndex, 0);
						lightViewIndex += 1;
						commandList->SetGraphicsRoot32BitConstants(4, 1, &lightViewIndex, 0);
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
};