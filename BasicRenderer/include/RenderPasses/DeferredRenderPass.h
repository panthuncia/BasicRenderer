#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Materials/Material.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Resources/TextureDescription.h"
#include "Resources/ResourceHandles.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/ObjectManager.h"

class DeferredRenderPass : public RenderPass {
public:
	DeferredRenderPass(
		int aoTextureDescriptorIndex,
		int normalsTextureDescriptorIndex,
		unsigned int albedoTextureDescriptorIndex,
		unsigned int metallicRoughnessTextureDescriptorIndex,
		unsigned int depthBufferDescriptorIndex) : 
		m_aoTextureDescriptorIndex(aoTextureDescriptorIndex),
		m_normalsTextureDescriptorIndex(normalsTextureDescriptorIndex), 
		m_albedoTextureDescriptorIndex(albedoTextureDescriptorIndex),
		m_metallicRoughnessTextureDescriptorIndex(metallicRoughnessTextureDescriptorIndex),
		m_depthBufferDescriptorIndex(depthBufferDescriptorIndex) {

		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();
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

		m_vertexBufferView = CreateFullscreenTriangleVertexBuffer(device.Get());
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

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		auto& dsvHandle = context.pPrimaryDepthBuffer->GetDSVInfos()[0].cpuHandle;
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.xRes, context.yRes);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.xRes, context.yRes);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		commandList->SetPipelineState(PSOManager::GetInstance().GetDeferredPSO(context.globalPSOFlags).Get());
		auto rootSignature = psoManager.GetRootSignature();
		commandList->SetGraphicsRootSignature(rootSignature.Get());

		unsigned int settings[NumSettingsRootConstants] = { getShadowsEnabled(), getPunctualLightingEnabled(), m_gtaoEnabled };
		commandList->SetGraphicsRoot32BitConstants(SettingsRootSignatureIndex, NumSettingsRootConstants, &settings, 0);

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;
		auto& lightManager = context.lightManager;

		staticBufferIndices[NormalMatrixBufferDescriptorIndex] = objectManager->GetNormalMatrixBufferSRVIndex();
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = meshManager->GetPostSkinningVertexBufferSRVIndex();
		staticBufferIndices[MeshletBufferDescriptorIndex] = meshManager->GetMeshletOffsetBufferSRVIndex();
		staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = meshManager->GetMeshletIndexBufferSRVIndex();
		staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = meshManager->GetMeshletTriangleBufferSRVIndex();
		staticBufferIndices[PerObjectBufferDescriptorIndex] = objectManager->GetPerObjectBufferSRVIndex();
		staticBufferIndices[CameraBufferDescriptorIndex] = cameraManager->GetCameraBufferSRVIndex();
		staticBufferIndices[PerMeshBufferDescriptorIndex] = meshManager->GetPerMeshBufferSRVIndex();
		staticBufferIndices[AOTextureDescriptorIndex] = m_aoTextureDescriptorIndex;
		staticBufferIndices[NormalsTextureDescriptorIndex] = m_normalsTextureDescriptorIndex;
		staticBufferIndices[AlbedoTextureDescriptorIndex] = m_albedoTextureDescriptorIndex;
		staticBufferIndices[MetallicRoughnessTextureDescriptorIndex] = m_metallicRoughnessTextureDescriptorIndex;
		commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int lightClusterInfo[NumLightClusterRootConstants] = {};
		lightClusterInfo[LightClusterBufferDescriptorIndex] = lightManager->GetClusterBuffer()->GetSRVInfo()[0].index;
		lightClusterInfo[LightPagesBufferDescriptorIndex] = lightManager->GetLightPagesBuffer()->GetSRVInfo()[0].index;
		commandList->SetGraphicsRoot32BitConstants(LightClusterRootSignatureIndex, NumLightClusterRootConstants, &lightClusterInfo, 0);

		unsigned int misc[NumMiscUintRootConstants] = {};
		misc[0] = m_depthBufferDescriptorIndex;

		commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

		unsigned int localPSOFlags = 0;
		if (getImageBasedLightingEnabled()) {
			localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
		}

		commandList->DrawInstanced(4, 1, 0, 0); // Fullscreen quad

		commandList->Close();
		return { { commandList.Get() } };
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

	unsigned int m_aoTextureDescriptorIndex;
	unsigned int m_normalsTextureDescriptorIndex;
	unsigned int m_albedoTextureDescriptorIndex;
	unsigned int m_metallicRoughnessTextureDescriptorIndex;
	unsigned int m_depthBufferDescriptorIndex;

	bool m_gtaoEnabled = true;

	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	std::shared_ptr<Buffer> m_vertexBufferHandle;
	// Define the vertices for the full-screen triangle

	struct FullscreenPassVertex {
		XMFLOAT3 position;
		XMFLOAT2 texcoord;
	};
	FullscreenPassVertex fullscreenTriangleVertices[4] = {
		{ XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0, 0.0)},
		{ XMFLOAT3(1.0f,  1.0f, 0.0f), XMFLOAT2(1.0, 0.0) },
		{ XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0, 1.0) },
		{ XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0, 1.0) }

	};
	// Create the vertex buffer for the full-screen triangle
	D3D12_VERTEX_BUFFER_VIEW CreateFullscreenTriangleVertexBuffer(ID3D12Device* device) {

		const UINT vertexBufferSize = static_cast<UINT>(4 * sizeof(FullscreenPassVertex));

		m_vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, (void*)fullscreenTriangleVertices);
		UploadManager::GetInstance().UploadData((void*)fullscreenTriangleVertices, vertexBufferSize, m_vertexBufferHandle.get(), 0);

		D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

		vertexBufferView.BufferLocation = m_vertexBufferHandle->m_buffer->GetGPUVirtualAddress();
		vertexBufferView.StrideInBytes = sizeof(FullscreenPassVertex);
		vertexBufferView.SizeInBytes = vertexBufferSize;

		return vertexBufferView;
	}
};