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
#include "Managers/Singletons/ECSManager.h"

class DebugSpherePass : public RenderPass {
public:
	DebugSpherePass() {
		CreateDebugRootSignature();
		CreateDebugMeshPSO();
	}
	~DebugSpherePass() {
	}

	void DeclareResourceUsages(RenderPassBuilder* builder) {
		builder->WithShaderResource(Builtin::PerObjectBuffer, Builtin::PerMeshBuffer, Builtin::CameraBuffer)
			.WithDepthReadWrite(Builtin::PrimaryCamera::DepthTexture)
			.IsGeometryPass();
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		m_opaqueMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		m_alphaTestMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		m_blendMeshInstancesQuery = ecsWorld.query_builder<Components::ObjectDrawInfo, Components::BlendMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
	
		m_pPrimaryDepthBuffer = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);

		m_cameraBufferSRVIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).index;
		m_objectBufferSRVIndex = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::PerObjectBuffer)->GetSRVInfo(0).index;
	}

	PassReturn Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::GetInstance();
		auto& commandList = context.commandList;

		ID3D12DescriptorHeap* descriptorHeaps[] = {
			context.textureDescriptorHeap, // The texture descriptor heap
			context.samplerDescriptorHeap, // The sampler descriptor heap
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		CD3DX12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, context.outputResolution.x, context.outputResolution.y);
		CD3DX12_RECT scissorRect = CD3DX12_RECT(0, 0, context.outputResolution.x, context.outputResolution.y);
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		// Set the render target
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		auto& dsvHandle = m_pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto rootSignature = m_debugRootSignature;
		commandList->SetGraphicsRootSignature(rootSignature.Get());


		auto pso = m_pso;
		commandList->SetPipelineState(pso.Get());
		
		struct Constants {
			float center[3];
			float padding;
			float radius;
			uint32_t perObjectIndex;
			uint32_t cameraBufferIndex;
			uint32_t objectBufferIndex;
		};
		Constants constants;
		constants.center[0] = 0.0;
		constants.center[1] = 0.0;
		constants.center[2] = 0.0;
		constants.radius = 1.0;
		constants.perObjectIndex = 0;
		constants.cameraBufferIndex = m_cameraBufferSRVIndex;
		constants.objectBufferIndex = m_objectBufferSRVIndex;

		commandList->SetGraphicsRoot32BitConstants(0, 8, &constants, 0);

		m_opaqueMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances opaqueMeshes) {
			auto& meshes = opaqueMeshes.meshInstances;

			for (auto& pMesh : meshes) {
				auto meshData = pMesh->GetMesh()->GetPerMeshCBData();
				constants.center[0] = meshData.boundingSphere.sphere.x;
				constants.center[1] = meshData.boundingSphere.sphere.y;
				constants.center[2] = meshData.boundingSphere.sphere.z;
				constants.radius = meshData.boundingSphere.sphere.w;
				constants.perObjectIndex = drawInfo.perObjectCBIndex;
				commandList->SetGraphicsRoot32BitConstants(0, 6, &constants, 0);
				commandList->DispatchMesh(1, 1, 1);
			}
			});

		m_alphaTestMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances alphaTestMeshes) {
			auto& meshes = alphaTestMeshes.meshInstances;

			for (auto& pMesh : meshes) {
				auto meshData = pMesh->GetMesh()->GetPerMeshCBData();
				constants.center[0] = meshData.boundingSphere.sphere.x;
				constants.center[1] = meshData.boundingSphere.sphere.y;
				constants.center[2] = meshData.boundingSphere.sphere.z;
				constants.radius = meshData.boundingSphere.sphere.w;
				constants.perObjectIndex = drawInfo.perObjectCBIndex;
				commandList->SetGraphicsRoot32BitConstants(0, 6, &constants, 0);
				commandList->DispatchMesh(1, 1, 1);
			}
			});

		m_blendMeshInstancesQuery.each([&](flecs::entity e, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances blendMeshes) {
			auto& meshes = blendMeshes.meshInstances;

			for (auto& pMesh : meshes) {
				auto meshData = pMesh->GetMesh()->GetPerMeshCBData();
				constants.center[0] = meshData.boundingSphere.sphere.x;
				constants.center[1] = meshData.boundingSphere.sphere.y;
				constants.center[2] = meshData.boundingSphere.sphere.z;
				constants.radius = meshData.boundingSphere.sphere.w;
				constants.perObjectIndex = drawInfo.perObjectCBIndex;
				commandList->SetGraphicsRoot32BitConstants(0, 6, &constants, 0);
				commandList->DispatchMesh(1, 1, 1);
			}
			});
		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:

	void CreateDebugRootSignature() {
		CD3DX12_DESCRIPTOR_RANGE1 debugDescriptorRangeSRV;

		CD3DX12_ROOT_PARAMETER1 debugRootParameters[1];
		debugRootParameters[0].InitAsConstants(8, 1, 0, D3D12_SHADER_VISIBILITY_MESH); 


		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(debugRootParameters), debugRootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

		ComPtr<ID3DBlob> serializedRootSig;
		ComPtr<ID3DBlob> errorBlob;
		HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &serializedRootSig, &errorBlob);
		if (FAILED(hr)) {
			if (errorBlob) {
				OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			}
			throw std::runtime_error("Failed to serialize root signature");
		}

		auto& device = DeviceManager::GetInstance().GetDevice();
		hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&m_debugRootSignature));
		if (FAILED(hr)) {
			throw std::runtime_error("Failed to create root signature");
		}
	}

	void CreateDebugMeshPSO() {

		auto manager = PSOManager::GetInstance();
		// Compile shaders
		Microsoft::WRL::ComPtr<ID3DBlob> meshShader;
		Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

		//manager.CompileShader(L"shaders/sphere.hlsl", L"MSMain", L"ms_6_6", {}, meshShader);
		//manager.CompileShader(L"shaders/sphere.hlsl", L"SpherePSMain", L"ps_6_6", {}, pixelShader);
		ShaderInfoBundle shaderInfoBundle;
		shaderInfoBundle.meshShader = { L"shaders/sphere.hlsl", L"MSMain", L"ms_6_6" };
		shaderInfoBundle.pixelShader = { L"shaders/sphere.hlsl", L"SpherePSMain", L"ps_6_6" };
		auto compiledBundle = manager.CompileShaders(shaderInfoBundle);
		meshShader = compiledBundle.meshShader;

		CD3DX12_RASTERIZER_DESC rasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		rasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		rasterizerState.FrontCounterClockwise = true;
		rasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		CD3DX12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

		CD3DX12_DEPTH_STENCIL_DESC depthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		depthStencilState.DepthEnable = TRUE;
		depthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

		DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_FORMAT dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

		struct PipelineStateStream {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_MS MS;
			CD3DX12_PIPELINE_STATE_STREAM_PS PS;
			CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
			CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
		};

		PipelineStateStream pipelineStateStream = {};
		pipelineStateStream.RootSignature = m_debugRootSignature.Get();
		pipelineStateStream.MS = CD3DX12_SHADER_BYTECODE(meshShader.Get());

		if (pixelShader) {
			pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		}

		pipelineStateStream.RasterizerState = rasterizerState;
		pipelineStateStream.BlendState = blendDesc;
		pipelineStateStream.DepthStencilState = depthStencilState;

		D3D12_RT_FORMAT_ARRAY rtvFormats = {};
		rtvFormats.NumRenderTargets = 1;
		rtvFormats.RTFormats[0] = rtvFormat;
		pipelineStateStream.RTVFormats = rtvFormats;

		pipelineStateStream.DSVFormat = dsvFormat;

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
		streamDesc.SizeInBytes = sizeof(PipelineStateStream);
		streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

		auto& device = DeviceManager::GetInstance().GetDevice();
		ID3D12Device2* device2 = nullptr;
		ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_pso)));

	}

	flecs::query<Components::ObjectDrawInfo, Components::OpaqueMeshInstances> m_opaqueMeshInstancesQuery;
	flecs::query<Components::ObjectDrawInfo, Components::AlphaTestMeshInstances> m_alphaTestMeshInstancesQuery;
	flecs::query<Components::ObjectDrawInfo, Components::BlendMeshInstances> m_blendMeshInstancesQuery;
	ComPtr<ID3D12RootSignature> m_debugRootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
	bool m_wireframe;

	std::shared_ptr<PixelBuffer> m_pPrimaryDepthBuffer;

	int m_cameraBufferSRVIndex = -1;
	int m_objectBufferSRVIndex = -1;

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

};