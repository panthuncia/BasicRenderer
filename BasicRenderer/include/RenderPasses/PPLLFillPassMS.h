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
#include "ResourceManager.h"
#include "TextureDescription.h"
#include "ResourceHandles.h"

class PPLLFillPassMS : public RenderPass {
public:
	PPLLFillPassMS(bool wireframe, uint16_t xRes, uint16_t yRes) {
		m_wireframe = wireframe;
		m_xRes = xRes;
		m_yRes = yRes;
		m_numPPLLNodes = xRes * yRes * m_aveFragsPerPixel;

		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		TextureDescription desc;
		desc.width = xRes;
		desc.height = yRes;
		desc.channels = 1;
		desc.format = DXGI_FORMAT_R32_UINT;
		desc.hasRTV = false;
		desc.hasUAV = true;
		desc.initialState = ResourceState::PIXEL_SRV;
		m_PPLLHeadPointerTexture = ResourceManager::GetInstance().CreateTexture(desc);
		m_PPLLBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_numPPLLNodes, m_PPLLNodeSize, ResourceState::UNORDERED_ACCESS, false, true, false);
		m_PPLLCounter = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(1, sizeof(unsigned int), ResourceState::UNORDERED_ACCESS, false, true, false);
	
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
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.dsvDescriptorSize);
		commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);

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

		unsigned int transparentPerMeshBufferIndex = meshManager->GetTransparentPerMeshBufferSRVIndex();
		commandList->SetGraphicsRoot32BitConstants(6, 1, &transparentPerMeshBufferIndex, 0);

		// PPLL heads & buffer
		uint32_t indices[4] = { m_PPLLHeadPointerTexture.SRVInfo.index, m_PPLLBuffer.dataBuffer->GetSRVInfo().index, m_PPLLCounter.dataBuffer->GetSRVInfo().index, m_numPPLLNodes};
		commandList->SetGraphicsRoot32BitConstants(7, 3, &indices, 0);

		for (auto& pair : context.currentScene->GetTransparentRenderableObjectIDMap()) {
			auto& renderable = pair.second;
			auto& meshes = renderable->GetTransparentMeshes();

			auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			commandList->SetGraphicsRoot32BitConstants(0, 1, &perObjectIndex, 0);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh;
				auto pso = psoManager.GetMeshPPLLPSO(localPSOFlags | mesh.material->m_psoFlags, BLEND_STATE_BLEND, m_wireframe);
				commandList->SetPipelineState(pso.Get());

				auto perMeshIndex = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				commandList->SetGraphicsRoot32BitConstants(1, 1, &perMeshIndex, 0);

				commandList->DispatchMesh(mesh.GetMeshletCount(), 1, 1);
			}
		}

		commandList->Close();
		return { commandList.Get() };
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	bool m_wireframe;

	uint16_t m_xRes;
	uint16_t m_yRes;
	size_t m_numPPLLNodes;

	TextureHandle<PixelBuffer> m_PPLLHeadPointerTexture;
	BufferHandle m_PPLLBuffer;
	BufferHandle m_PPLLCounter;

	// PPLL settings
	static const size_t m_aveFragsPerPixel = 12;
	static const size_t m_PPLLNodeSize = 16;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;
};