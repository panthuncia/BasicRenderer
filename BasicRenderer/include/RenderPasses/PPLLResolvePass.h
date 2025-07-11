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
#include "Managers/Singletons/UploadManager.h"

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

	void DeclareResourceUsages(RenderPassBuilder* builder) {
		builder->WithShaderResource(Builtin::PPLL::HeadPointerTexture, Builtin::PPLL::Buffer)
			.WithRenderTarget(Builtin::Color::HDRColorTarget);
	}

	void Setup(const ResourceRegistryView& resourceRegistryView) override {
		CreatePSO();

		m_pHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
	}

	PassReturn Execute(RenderContext& context) override {

		auto numBlend = context.drawStats.numBlendDraws;
		if (numBlend == 0) {
			return {};
		}

		auto& psoManager = PSOManager::GetInstance();
		auto& commandList = context.commandList;

		ID3D12DescriptorHeap* descriptorHeaps[] = {
			context.textureDescriptorHeap, // The texture descriptor heap
			context.samplerDescriptorHeap, // The sampler descriptor heap
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		//CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		auto rtvHandle = m_pHDRTarget->GetRTVInfo(0).cpuHandle;
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.renderResolution.x, context.renderResolution.y);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.renderResolution.x, context.renderResolution.y);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		commandList->SetPipelineState(pso.Get());
		auto rootSignature = psoManager.GetRootSignature();
		commandList->SetGraphicsRootSignature(rootSignature.Get());

		unsigned int settings[2] = { getShadowsEnabled(), getPunctualLightingEnabled() }; // HLSL bools are 32 bits
		unsigned int punctualLightingEnabled = getPunctualLightingEnabled();
		commandList->SetGraphicsRoot32BitConstants(SettingsRootSignatureIndex, 2, &settings, 0);

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;
		//staticBufferIndices[NormalMatrixBufferDescriptorIndex] = m_normalMatrixBufferSRVIndex;
		//staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = m_postSkinningVertexBufferSRVIndex;
		//staticBufferIndices[MeshletBufferDescriptorIndex] = m_meshletOffsetBufferSRVIndex;
		//staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = m_meshletVertexIndexBufferSRVIndex;
		//staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = m_meshletTriangleBufferSRVIndex;
		//staticBufferIndices[PerObjectBufferDescriptorIndex] = m_perObjectBufferSRVIndex;
		//staticBufferIndices[CameraBufferDescriptorIndex] = m_cameraBufferSRVIndex;
		//staticBufferIndices[PerMeshBufferDescriptorIndex] = m_perMeshBufferSRVIndex;

		unsigned int transparencyInfo[NumTransparencyInfoRootConstants] = {};
		transparencyInfo[PPLLHeadBufferDescriptorIndex] = m_PPLLHeadPointerTexture->GetSRVInfo(0).index;
		transparencyInfo[PPLLNodeBufferDescriptorIndex] = m_PPLLBuffer->GetSRVInfo(0).index;
		commandList->SetGraphicsRoot32BitConstants(TransparencyInfoRootSignatureIndex, NumTransparencyInfoRootConstants, &transparencyInfo, 0);

		commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int localPSOFlags = 0;
		if (getImageBasedLightingEnabled()) {
			localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
		}

		// PPLL heads & buffer
		uint32_t indices[4] = { m_PPLLHeadPointerTexture->GetSRVInfo(0).index, m_PPLLBuffer->GetSRVInfo(0).index };
		commandList->SetGraphicsRoot32BitConstants(TransparencyInfoRootSignatureIndex, 2, &indices, 0);

		commandList->DrawInstanced(4, 1, 0, 0); // Fullscreen quad
		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	ComPtr<ID3D12PipelineState> pso;

	std::shared_ptr<PixelBuffer> m_pHDRTarget;
	std::shared_ptr<PixelBuffer> m_PPLLHeadPointerTexture;
	std::shared_ptr<Buffer> m_PPLLBuffer;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

	void CreatePSO() {
		// Compile shaders
		Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
		Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
		PSOManager::GetInstance().CompileShader(L"shaders/fullscreenVS.hlsli", L"FullscreenVSMain", L"vs_6_6", {}, vertexShader);
		PSOManager::GetInstance().CompileShader(L"shaders/PPLL.hlsl", L"PPLLResolvePS", L"ps_6_6", {}, pixelShader);

		D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
		inputLayoutDesc.pInputElementDescs = nullptr;
		inputLayoutDesc.NumElements = 0;

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

		DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = inputLayoutDesc;
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
};