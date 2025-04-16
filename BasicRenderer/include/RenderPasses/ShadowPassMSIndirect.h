#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "mesh.h"
#include "Scene.h"
#include "Resources/ResourceGroup.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Managers/MeshManager.h"
#include "Managers/ObjectManager.h"
#include "Managers/CameraManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "MeshInstance.h"

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
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::ShadowMap>().build();
	}

	RenderPassReturn Execute(RenderContext& context) override {
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

		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
		staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
		staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
		staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
		staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = meshManager->GetPerMeshInstanceBufferSRVIndex();
		staticBufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetPerMeshBufferSRVIndex();

		commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchMeshCommandSignature();

		auto drawObjects = [&](ID3D12Resource* opaqueIndirectCommandBuffer, ID3D12Resource* alphaTestIndirectCommandBuffer, ID3D12Resource* blendIndirectCommandBuffer, size_t opaqueCommandCounterOffset, size_t alphaTestCommandCounterOffset, size_t blendIndirectCommandCounterOffset) {
			auto numOpaque = context.drawStats.numOpaqueDraws;
			if (numOpaque != 0) {
				auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW, BlendState::BLEND_STATE_OPAQUE, false);
				commandList->SetPipelineState(pso.Get());
				commandList->ExecuteIndirect(commandSignature, numOpaque, opaqueIndirectCommandBuffer, 0, opaqueIndirectCommandBuffer, opaqueCommandCounterOffset);
			}

			auto numAlphaTest = context.drawStats.numAlphaTestDraws;
			if (numAlphaTest != 0) {
				auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW | PSOFlags::PSO_ALPHA_TEST, BlendState::BLEND_STATE_MASK, false);
				commandList->SetPipelineState(pso.Get());
				commandList->ExecuteIndirect(commandSignature, numAlphaTest, alphaTestIndirectCommandBuffer, 0, alphaTestIndirectCommandBuffer, alphaTestCommandCounterOffset);
			}

			auto numBlend = context.drawStats.numBlendDraws;
			if (numBlend != 0) {
				auto pso = psoManager.GetMeshPSO(PSOFlags::PSO_SHADOW | PSOFlags::PSO_BLEND, BlendState::BLEND_STATE_BLEND, false);
				commandList->SetPipelineState(pso.Get());
				commandList->ExecuteIndirect(commandSignature, numBlend, blendIndirectCommandBuffer, 0, blendIndirectCommandBuffer, blendIndirectCommandCounterOffset);
			}
		};

		lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::ShadowMap shadowMap) {
			float clear[4] = { 1.0, 0.0, 0.0, 0.0 };
			switch (light.type) {
			case Components::LightType::Spot: {
				auto& dsvHandle = shadowMap.shadowMap->GetBuffer()->GetDSVInfos()[0].cpuHandle;
				commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
				commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
				int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewInfo.viewInfoBufferIndex };
				commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightInfo, 0);
				auto& opaque = lightViewInfo.commandBuffers.opaqueIndirectCommandBuffers[0];
				auto& alphaTest = lightViewInfo.commandBuffers.alphaTestIndirectCommandBuffers[0];
				auto& blend = lightViewInfo.commandBuffers.blendIndirectCommandBuffers[0];
				drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
				break;
			}
			case Components::LightType::Point: {
				int lightViewIndex = lightViewInfo.viewInfoBufferIndex * 6;
				int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
				commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightInfo, 0);
				for (int i = 0; i < 6; i++) {
					D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMap.shadowMap->GetBuffer()->GetDSVInfos()[i].cpuHandle;
					commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
					commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
					commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightViewIndex, LightViewIndex);
					lightViewIndex += 1;
					auto& opaque = lightViewInfo.commandBuffers.opaqueIndirectCommandBuffers[i];
					auto& alphaTest = lightViewInfo.commandBuffers.alphaTestIndirectCommandBuffers[i];
					auto& blend = lightViewInfo.commandBuffers.blendIndirectCommandBuffers[i];
					drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
				}
				break;
			}
			case Components::LightType::Directional: {
				//int lightIndex = light->GetCurrentLightBufferIndex();
				int lightViewIndex = lightViewInfo.viewInfoBufferIndex * getNumDirectionalLightCascades();
				int lightInfo[2] = { lightViewInfo.lightBufferIndex, lightViewIndex };
				for (int i = 0; i < getNumDirectionalLightCascades(); i++) {
					auto& dsvHandle = shadowMap.shadowMap->GetBuffer()->GetDSVInfos()[i].cpuHandle;
					commandList->OMSetRenderTargets(0, nullptr, TRUE, &dsvHandle);
					commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
					commandList->SetGraphicsRoot32BitConstants(ViewRootSignatureIndex, 1, &lightViewIndex, LightViewIndex);
					lightViewIndex += 1;
					auto& opaque = lightViewInfo.commandBuffers.opaqueIndirectCommandBuffers[i];
					auto& alphaTest = lightViewInfo.commandBuffers.alphaTestIndirectCommandBuffers[i];
					auto& blend = lightViewInfo.commandBuffers.blendIndirectCommandBuffers[i];
					drawObjects(opaque->GetAPIResource(), alphaTest->GetAPIResource(), blend->GetAPIResource(), opaque->GetResource()->GetUAVCounterOffset(), alphaTest->GetResource()->GetUAVCounterOffset(), blend->GetResource()->GetUAVCounterOffset());
				}
			}
			}
			});
		commandList->Close();
		return { { commandList.Get()} };
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	flecs::query<Components::Light, Components::LightViewInfo, Components::ShadowMap> lightQuery;
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<uint16_t()> getShadowResolution;
};