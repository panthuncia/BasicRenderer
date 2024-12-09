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

class PPLLResolvePass : public RenderPass {
public:
	PPLLResolvePass(std::shared_ptr<PixelBuffer> PPLLHeads, std::shared_ptr<Buffer> PPLLBuffer) {

		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");

		m_PPLLHeadPointerTexture = PPLLHeads;
		m_PPLLBuffer = PPLLBuffer;
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
		CreatePSO();
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

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.dsvDescriptorSize);
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.xRes, context.yRes);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.xRes, context.yRes);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		commandList->SetPipelineState(pso.Get());
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

		unsigned int transparentPerMeshBufferIndex = meshManager->GetBlendPerMeshBufferSRVIndex();
		commandList->SetGraphicsRoot32BitConstants(6, 1, &transparentPerMeshBufferIndex, 0);

		// PPLL heads & buffer
		uint32_t indices[4] = { m_PPLLHeadPointerTexture->GetSRVInfo().index, m_PPLLBuffer->GetSRVInfo().index };
		commandList->SetGraphicsRoot32BitConstants(7, 2, &indices, 0);

		commandList->DrawInstanced(4, 1, 0, 0); // Fullscreen quad

		commandList->Close();
		return { commandList.Get() };
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	std::vector<ComPtr<ID3D12GraphicsCommandList7>> m_commandLists;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
	ComPtr<ID3D12PipelineState> pso;

	std::shared_ptr<PixelBuffer> m_PPLLHeadPointerTexture;
	std::shared_ptr<Buffer> m_PPLLBuffer;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

	void CreatePSO() {
		// Compile shaders
		Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
		Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
		PSOManager::GetInstance().CompileShader(L"shaders/PPLL.hlsl", L"VSMain", L"vs_6_6", {}, vertexShader);
		PSOManager::GetInstance().CompileShader(L"shaders/PPLL.hlsl", L"PPLLResolvePS", L"ps_6_6", {}, pixelShader);

		static D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
		inputLayoutDesc.pInputElementDescs = inputElementDescs;
		inputLayoutDesc.NumElements = _countof(inputElementDescs);

		D3D12_RASTERIZER_DESC rasterizerDesc = {};
		rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
		rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // No culling for full-screen triangle
		rasterizerDesc.FrontCounterClockwise = FALSE;
		rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		rasterizerDesc.DepthClipEnable = FALSE;
		rasterizerDesc.MultisampleEnable = FALSE;
		rasterizerDesc.AntialiasedLineEnable = FALSE;
		rasterizerDesc.ForcedSampleCount = 0;
		rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		D3D12_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = FALSE;
		blendDesc.IndependentBlendEnable = FALSE;
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
		depthStencilDesc.DepthEnable = FALSE;
		depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		depthStencilDesc.StencilEnable = FALSE;
		depthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		depthStencilDesc.BackFace = depthStencilDesc.FrontFace;

		DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = inputLayoutDesc;   // No input layout needed for full-screen triangle
		psoDesc.pRootSignature = PSOManager::GetInstance().GetRootSignature().Get();
		psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
		psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
		psoDesc.RasterizerState = rasterizerDesc;
		psoDesc.BlendState = blendDesc;
		psoDesc.DepthStencilState = depthStencilDesc;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = renderTargetFormat;
		psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;
		psoDesc.InputLayout = inputLayoutDesc;

		auto& device = DeviceManager::GetInstance().GetDevice();
		auto hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
		if (FAILED(hr)) {
			throw std::runtime_error("Failed to create debug PSO");
		}
	}

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

		// Create a default heap for the vertex buffer
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

		m_vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, ResourceState::VERTEX, (void*)fullscreenTriangleVertices);
		UploadManager::GetInstance().UploadData((void*)fullscreenTriangleVertices, vertexBufferSize, m_vertexBufferHandle.get(), 0);

		D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};

		vertexBufferView.BufferLocation = m_vertexBufferHandle->m_buffer->GetGPUVirtualAddress();
		vertexBufferView.StrideInBytes = sizeof(FullscreenPassVertex);
		vertexBufferView.SizeInBytes = vertexBufferSize;

		return vertexBufferView;
	}
};