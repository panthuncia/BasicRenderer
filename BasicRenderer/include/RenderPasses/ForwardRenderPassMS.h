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

class ForwardRenderPassMS : public RenderPass {
public:
	ForwardRenderPassMS(bool wireframe) {
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
		staticBufferIndices[0] = meshManager->GetPostSkinningVertexBufferSRVIndex();
		staticBufferIndices[1] = meshManager->GetMeshletOffsetBufferSRVIndex();
		staticBufferIndices[2] = meshManager->GetMeshletIndexBufferSRVIndex();
		staticBufferIndices[3] = meshManager->GetMeshletTriangleBufferSRVIndex();
		staticBufferIndices[4] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[5] = cameraManager->GetCameraBufferSRVIndex();

		commandList->SetGraphicsRoot32BitConstants(5, 6, &staticBufferIndices, 0);

		unsigned int localPSOFlags = 0;
		if (getImageBasedLightingEnabled()) {
			localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
		}
		
		unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
		commandList->SetGraphicsRoot32BitConstants(6, 1, &opaquePerMeshBufferIndex, 0);

		for (auto& pair : context.currentScene->GetOpaqueRenderableObjectIDMap()) {
			auto& renderable = pair.second;
			auto& meshes = renderable->GetOpaqueMeshes();

			auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			commandList->SetGraphicsRoot32BitConstants(0, 1, &perObjectIndex, 0);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				auto pso = psoManager.GetMeshPSO(localPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
				commandList->SetPipelineState(pso.Get());

				unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
				perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
				commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

				commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
			}
		}

		unsigned int transparentPerMeshBufferIndex = meshManager->GetAlphaTestPerMeshBufferSRVIndex();
		commandList->SetGraphicsRoot32BitConstants(6, 1, &transparentPerMeshBufferIndex, 0);

		for (auto& pair : context.currentScene->GetAlphaTestRenderableObjectIDMap()) {
			auto& renderable = pair.second;
			auto& meshes = renderable->GetAlphaTestMeshes();

			auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			commandList->SetGraphicsRoot32BitConstants(0, 1, &perObjectIndex, 0);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				auto pso = psoManager.GetMeshPSO(localPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
				commandList->SetPipelineState(pso.Get());

				unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
				perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
				commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

				commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
			}
		}

		commandList->Close();
		return { { commandList.Get() } };
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