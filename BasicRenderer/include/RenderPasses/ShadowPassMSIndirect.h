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

	PassReturn Execute(RenderContext& context) override {
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

		auto& meshManager = context.currentScene->GetMeshManager();
		auto& objectManager = context.currentScene->GetObjectManager();
		auto& cameraManager = context.currentScene->GetCameraManager();

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
		staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
		staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
		staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();

		commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		auto commandSignature = context.currentScene->GetIndirectCommandBufferManager()->GetCommandSignature();

		auto drawObjects = [&](ID3D12Resource* opaqueIndirectCommandBuffer, ID3D12Resource* alphaTestIndirectCommandBuffer, ID3D12Resource* blendIndirectCommandBuffer, size_t opaqueCommandCounterOffset, size_t alphaTestCommandCounterOffset, size_t blendIndirectCommandCounterOffset) {
			auto numOpaque = context.currentScene->GetNumOpaqueDraws();
			if (numOpaque != 0) {
				unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
				commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &opaquePerMeshBufferIndex, PerMeshBufferDescriptorIndex);

				auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW, BlendState::BLEND_STATE_OPAQUE, false);
				commandList->SetPipelineState(pso.Get());
				commandList->ExecuteIndirect(commandSignature.Get(), numOpaque, opaqueIndirectCommandBuffer, 0, opaqueIndirectCommandBuffer, opaqueCommandCounterOffset);
			}

			auto numAlphaTest = context.currentScene->GetNumAlphaTestDraws();
			if (numAlphaTest != 0) {
				unsigned int alphaTestPerMeshBufferIndex = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
				commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &alphaTestPerMeshBufferIndex, PerMeshBufferDescriptorIndex);

				auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW | PSOFlags::PSO_ALPHA_TEST, BlendState::BLEND_STATE_MASK, false);
				commandList->SetPipelineState(pso.Get());
				commandList->ExecuteIndirect(commandSignature.Get(), numAlphaTest, alphaTestIndirectCommandBuffer, 0, alphaTestIndirectCommandBuffer, alphaTestCommandCounterOffset);
			}

			auto numBlend = context.currentScene->GetNumBlendDraws();
			if (numBlend != 0) {
				unsigned int blendPerMeshBufferIndex = meshManager->GetBlendPerMeshBufferSRVIndex();
				commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &blendPerMeshBufferIndex, PerMeshBufferDescriptorIndex);

				auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW | PSOFlags::PSO_BLEND, BlendState::BLEND_STATE_BLEND, false);
				commandList->SetPipelineState(pso.Get());
				commandList->ExecuteIndirect(commandSignature.Get(), numBlend, blendIndirectCommandBuffer, 0, blendIndirectCommandBuffer, blendIndirectCommandCounterOffset);
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
					auto& opaque = light->GetPerViewOpaqueIndirectCommandBuffers()[0];
					auto& alphaTest = light->GetPerViewAlphaTestIndirectCommandBuffers()[0];
					auto& blend = light->GetPerViewBlendIndirectCommandBuffers()[0];
					drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
					break;
				}
				case LightType::Point: {
					int lightIndex = light->GetCurrentLightBufferIndex();
					int lightViewIndex = light->GetCurrentviewInfoIndex()*6;
					commandList->SetGraphicsRoot32BitConstants(2, 1, &lightIndex, 0);
					auto& opaqueBuffers = light->GetPerViewOpaqueIndirectCommandBuffers();
					auto& transparentBuffers = light->GetPerViewAlphaTestIndirectCommandBuffers();
					for (int i = 0; i < 6; i++) {
						D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMap->GetBuffer()->GetDSVInfos()[i].cpuHandle;
						commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
						commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
						commandList->SetGraphicsRoot32BitConstants(3, 1, &lightViewIndex, 0);
						lightViewIndex += 1;
						auto& opaque = light->GetPerViewOpaqueIndirectCommandBuffers()[i];
						auto& alphaTest = light->GetPerViewAlphaTestIndirectCommandBuffers()[i];
						auto& blend = light->GetPerViewBlendIndirectCommandBuffers()[i];
						drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
					}
					break;
				}
					case LightType::Directional: {
					int lightViewIndex = light->GetCurrentviewInfoIndex()*getNumDirectionalLightCascades();
					int lightIndex = light->GetCurrentLightBufferIndex();
					commandList->SetGraphicsRoot32BitConstants(2, 1, &lightIndex, 0);
					auto& opaqueBuffers = light->GetPerViewOpaqueIndirectCommandBuffers();
					auto& transparentBuffers = light->GetPerViewAlphaTestIndirectCommandBuffers();
					for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
						auto& dsvHandle = shadowMap->GetBuffer()->GetDSVInfos()[i].cpuHandle;
						commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
						commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
						commandList->SetGraphicsRoot32BitConstants(3, 1, &lightViewIndex, 0);
						lightViewIndex += 1;
						auto& opaque = light->GetPerViewOpaqueIndirectCommandBuffers()[i];
						auto& alphaTest = light->GetPerViewAlphaTestIndirectCommandBuffers()[i];
						auto& blend = light->GetPerViewBlendIndirectCommandBuffers()[i];
						drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
					}
				}
			}
		}
		commandList->Close();
		return { { commandList.Get()} };
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