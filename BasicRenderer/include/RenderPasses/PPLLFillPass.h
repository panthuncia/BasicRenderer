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
#include "UploadManager.h"

class PPLLFillPass : public RenderPass {
public:
	PPLLFillPass(bool wireframe, std::shared_ptr<PixelBuffer> PPLLHeads, std::shared_ptr<Buffer> PPLLBuffer, std::shared_ptr<Buffer> PPLLCounter, size_t numPPLLNodes) {
		m_wireframe = wireframe;

		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");

		m_PPLLHeadPointerTexture = PPLLHeads;
		m_PPLLBuffer = PPLLBuffer;
		m_PPLLCounter = PPLLCounter;
		m_numPPLLNodes = numPPLLNodes;
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

		auto numBlend = context.currentScene->GetNumBlendDraws();
		if (numBlend == 0) {
			return {};
		}

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

		uint32_t clearValues[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff }; // Null values
		commandList->ClearUnorderedAccessViewUint(m_PPLLHeadPointerTexture->GetUAVShaderVisibleInfo().gpuHandle, m_PPLLHeadPointerTexture->GetUAVNonShaderVisibleInfo().cpuHandle, m_PPLLHeadPointerTexture->GetAPIResource(), clearValues, 0, nullptr);

		D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_PPLLHeadPointerTexture->GetAPIResource());
		commandList->ResourceBarrier(1, &uavBarrier);

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
		commandList->SetGraphicsRoot32BitConstants(SettingsRootSignatureIndex, 2, &settings, 0);

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		auto& meshManager = context.currentScene->GetMeshManager();
		auto& objectManager = context.currentScene->GetObjectManager();
		auto& cameraManager = context.currentScene->GetCameraManager();
		staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
		staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
		staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
		staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
		staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = meshManager->GetPerMeshInstanceBufferSRVIndex();

		commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int localPSOFlags = 0;
		if (getImageBasedLightingEnabled()) {
			localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
		}

		unsigned int transparentPerMeshBufferIndex = meshManager->GetBlendPerMeshBufferSRVIndex();
		commandList->SetGraphicsRoot32BitConstants(VariableBufferRootSignatureIndex, 1, &transparentPerMeshBufferIndex, PerMeshBufferDescriptorIndex);

		// PPLL heads & buffer
		uint32_t indices[4] = { m_PPLLHeadPointerTexture->GetUAVShaderVisibleInfo().index, m_PPLLBuffer->GetUAVShaderVisibleInfo().index, m_PPLLCounter->GetUAVShaderVisibleInfo().index, m_numPPLLNodes };
		commandList->SetGraphicsRoot32BitConstants(TransparencyInfoRootSignatureIndex, 4, &indices, 0);

		for (auto& pair : context.currentScene->GetBlendRenderableObjectIDMap()) {
			auto& renderable = pair.second;
			auto& meshes = renderable->GetBlendMeshes();

			auto perObjectIndex = renderable->GetCurrentPerObjectCBView()->GetOffset() / sizeof(PerObjectCB);
			commandList->SetGraphicsRoot32BitConstants(PerObjectRootSignatureIndex, 1, &perObjectIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				auto pso = psoManager.GetPPLLPSO(localPSOFlags | mesh.material->m_psoFlags, BLEND_STATE_BLEND, m_wireframe);
				commandList->SetPipelineState(pso.Get());

				unsigned int perMeshIndices[NumPerMeshRootConstants] = {};
				perMeshIndices[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				perMeshIndices[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
				commandList->SetGraphicsRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, perMeshIndices, 0);

				D3D12_INDEX_BUFFER_VIEW indexBufferView = mesh.GetIndexBufferView();
				commandList->IASetIndexBuffer(&indexBufferView);

				commandList->DrawIndexedInstanced(mesh.GetIndexCount(), 1, 0, 0, 0);
			}
		}

		commandList->Close();
		return { { commandList.Get() } };
	}

	virtual void Update() override {
		// Reset UAV counter
		uint32_t zero = 0;
		UploadManager::GetInstance().UploadData(&zero, sizeof(uint32_t), m_PPLLCounter.get(), 0);
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	bool m_wireframe;

	size_t m_numPPLLNodes;

	std::shared_ptr<PixelBuffer> m_PPLLHeadPointerTexture;
	std::shared_ptr<Buffer> m_PPLLBuffer;
	std::shared_ptr<Buffer> m_PPLLCounter;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;
};