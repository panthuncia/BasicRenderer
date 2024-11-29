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
		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator)));
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
		m_commandList->Close();
	}

	std::vector<ID3D12GraphicsCommandList*> Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::GetInstance();
		auto commandList = m_commandList.Get();
		ThrowIfFailed(m_allocator->Reset());
		commandList->Reset(m_allocator.Get(), nullptr);

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
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart());
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

		//D3D12_GPU_VIRTUAL_ADDRESS objectBufferAddress = context.currentScene->GetObjectManager()->GetPerObjectBuffers()->GetBuffer()->m_buffer->GetGPUVirtualAddress();
		//D3D12_GPU_VIRTUAL_ADDRESS perMeshBufferAddress = context.currentScene->GetMeshManager()->GetPerMeshBuffers()->GetBuffer()->m_buffer->GetGPUVirtualAddress();
		
		unsigned int opaquePerMeshBufferIndex = meshManager->GetOpaquePerMeshBufferSRVIndex();
		commandList->SetGraphicsRoot32BitConstants(6, 1, &opaquePerMeshBufferIndex, 0);

		for (auto& pair : context.currentScene->GetOpaqueRenderableObjectIDMap()) {
			auto& renderable = pair.second;
			auto& meshes = renderable->GetOpaqueMeshes();

			auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			commandList->SetGraphicsRoot32BitConstants(0, 1, &perObjectIndex, 0);
			/*size_t offset = renderable->GetCurrentPerObjectCBView()->GetOffset();
			commandList->SetGraphicsRootConstantBufferView(0, objectBufferAddress+offset);*/

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh;
				auto pso = psoManager.GetMeshPSO(localPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
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
				auto pso = psoManager.GetMeshPSO(localPSOFlags | mesh.material->m_psoFlags, mesh.material->m_blendState, m_wireframe);
				commandList->SetPipelineState(pso.Get());

				auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				commandList->SetGraphicsRoot32BitConstants(1, 1, &perMeshIndex, 0);
				//auto offset = mesh.GetPerMeshBufferView()->GetOffset();
				//commandList->SetGraphicsRootConstantBufferView(1, perMeshBufferAddress + offset);

				commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
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
	bool m_wireframe;
	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

};