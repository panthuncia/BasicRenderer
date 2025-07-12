#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/MeshManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "../../shaders/PerPassRootConstants/meshletCullingRootConstants.h"

class MeshletCullingPass : public ComputePass {
public:
	MeshletCullingPass(bool isOccludersPass, bool isRemaindersPass = false, bool doResets = true) :
		m_isOccludersPass(isOccludersPass), 
		m_isRemaindersPass(isRemaindersPass),
		m_doResets(doResets){
		getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
		getShadowsEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableShadows");
		m_occlusionCullingEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableOcclusionCulling")();
	}

	~MeshletCullingPass() {
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::Light, Components::LightViewInfo, Components::DepthMap>().cached().cache_kind(flecs::QueryCacheAll).build();

		CreatePSO();

		RegisterSRV(Builtin::PerObjectBuffer);
		RegisterSRV(Builtin::CameraBuffer);
		RegisterSRV(Builtin::PerMeshBuffer);
		RegisterSRV(Builtin::PerMeshInstanceBuffer);
		RegisterSRV(Builtin::MeshResources::MeshletBounds);

		m_primaryCameraMeshletCullingBitfieldBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::MeshletBitfield);
		m_primaryCameraMeshletCullingIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletFrustrumCulling);
		m_primaryCameraMeshletCullingResetIndirectCommandBuffer = m_resourceRegistryView->Request<DynamicGloballyIndexedResource>(Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset);

		m_primaryCameraLinearDepthMap = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::LinearDepthMap);
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		builder->WithShaderResource(Builtin::PerObjectBuffer, 
			Builtin::PerMeshBuffer, 
			Builtin::PerMeshInstanceBuffer, 
			Builtin::MeshResources::MeshletBounds, 
			Builtin::CameraBuffer,
			Builtin::PrimaryCamera::LinearDepthMap,
			Builtin::Shadows::LinearShadowMaps)
			.WithUnorderedAccess(Builtin::MeshletCullingBitfieldGroup, Builtin::PrimaryCamera::MeshletBitfield)
			.WithIndirectArguments(
				Builtin::IndirectCommandBuffers::MeshletCulling, 
				Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletFrustrumCulling, 
				Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset);
	}

	PassReturn Execute(RenderContext& context) override {

		unsigned int numDraws = context.drawStats.numOpaqueDraws + context.drawStats.numAlphaTestDraws + context.drawStats.numBlendDraws;

		if (numDraws == 0) {
			return {};
		}

		auto& commandList = context.commandList;

		// Set the descriptor heaps
		ID3D12DescriptorHeap* descriptorHeaps[] = {
			ResourceManager::GetInstance().GetSRVDescriptorHeap().Get(),
			ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		auto rootSignature = PSOManager::GetInstance().GetRootSignature();
		commandList->SetComputeRootSignature(rootSignature.Get());

		// Set the compute pipeline state
		commandList->SetPipelineState(m_frustrumCullingPSO.Get());

		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;

		BindResourceDescriptorIndices(commandList, m_resourceDescriptorBindings);

		unsigned int miscRootConstants[NumMiscUintRootConstants] = {};
		miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = m_primaryCameraMeshletCullingBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
		miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = m_primaryCameraLinearDepthMap->GetSRVInfo(0).index;

		commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);

		unsigned int cameraIndex = context.currentScene->GetPrimaryCamera().get<Components::RenderView>().cameraBufferIndex;
		commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

		// Culling for main camera

		// Frustrum culling
		auto meshletCullingBuffer = m_primaryCameraMeshletCullingIndirectCommandBuffer;
		
		auto commandSignature = CommandSignatureManager::GetInstance().GetDispatchCommandSignature();
		commandList->ExecuteIndirect(
			commandSignature,
			numDraws,
			meshletCullingBuffer->GetResource()->GetAPIResource(),
			0,
			meshletCullingBuffer->GetResource()->GetAPIResource(),
			meshletCullingBuffer->GetResource()->GetUAVCounterOffset()
		);

		// Reset necessary meshlets
		if (m_doResets) {
			auto meshletCullingClearBuffer = m_primaryCameraMeshletCullingResetIndirectCommandBuffer;
			commandList->SetPipelineState(m_clearPSO.Get());

			commandList->ExecuteIndirect(
				commandSignature,
				numDraws,
				meshletCullingClearBuffer->GetResource()->GetAPIResource(),
				0,
				meshletCullingClearBuffer->GetResource()->GetAPIResource(),
				meshletCullingClearBuffer->GetResource()->GetUAVCounterOffset()
			);
		}

		if (getShadowsEnabled()) {

			// Frustrum culling
			commandList->SetPipelineState(m_frustrumCullingPSO.Get());
			lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {

				for (auto& view : lightViewInfo.renderViews) {

					cameraIndex = view.cameraBufferIndex;
					commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

					miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
					miscRootConstants[LINEAR_DEPTH_MAP_SRV_DESCRIPTOR_INDEX] = light.type == Components::LightType::Point ? lightDepth.linearDepthMap->GetSRVInfo(SRVViewType::Texture2DArray, 0).index : lightDepth.linearDepthMap->GetSRVInfo(0).index;
					commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);
					meshletCullingBuffer = view.indirectCommandBuffers.meshletCullingIndirectCommandBuffer;
					commandList->ExecuteIndirect(
						commandSignature,
						numDraws,
						meshletCullingBuffer->GetResource()->GetAPIResource(),
						0,
						meshletCullingBuffer->GetResource()->GetAPIResource(),
						meshletCullingBuffer->GetResource()->GetUAVCounterOffset()
					);
				}
				});

			// Reset necessary meshlets
			if (m_doResets) {
				commandList->SetPipelineState(m_clearPSO.Get());
				lightQuery.each([&](flecs::entity e, Components::Light light, Components::LightViewInfo& lightViewInfo, Components::DepthMap lightDepth) {

					for (auto& view : lightViewInfo.renderViews) {

						cameraIndex = view.cameraBufferIndex;
						commandList->SetComputeRoot32BitConstants(ViewRootSignatureIndex, 1, &cameraIndex, LightViewIndex);

						miscRootConstants[MESHLET_CULLING_BITFIELD_BUFFER_UAV_DESCRIPTOR_INDEX] = view.meshletBitfieldBuffer->GetResource()->GetUAVShaderVisibleInfo(0).index;
						commandList->SetComputeRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &miscRootConstants, 0);
						auto meshletCullingClearBuffer = view.indirectCommandBuffers.meshletCullingResetIndirectCommandBuffer;
						commandList->ExecuteIndirect(
							commandSignature,
							numDraws,
							meshletCullingClearBuffer->GetResource()->GetAPIResource(),
							0,
							meshletCullingClearBuffer->GetResource()->GetAPIResource(),
							meshletCullingClearBuffer->GetResource()->GetUAVCounterOffset()
						);
					}
					});
			}
		}

		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		DxcDefine occludersDefine;
		occludersDefine.Name = L"OCCLUDERS_PASS";
		occludersDefine.Value = L"1";
		std::vector<DxcDefine> defines;
		if (m_isOccludersPass) {
			defines.push_back(occludersDefine);
		}
		if (m_isRemaindersPass) {
			DxcDefine remainderDefine;
			remainderDefine.Name = L"REMAINDERS_PASS";
			remainderDefine.Value = L"1";
			defines.push_back(remainderDefine);
		}
		if (m_occlusionCullingEnabled) {
			DxcDefine occlusionDefine;
			occlusionDefine.Name = L"OCCLUSION_CULLING";
			occlusionDefine.Value = L"1";
			defines.push_back(occlusionDefine);
		}
		ShaderInfoBundle shaderInfoBundle;
		shaderInfoBundle.computeShader = { L"shaders/meshletCulling.hlsl", L"MeshletFrustrumCullingCSMain", L"cs_6_6" };
		shaderInfoBundle.defines = defines;
		auto compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
		computeShader = compiledBundle.computeShader;
		m_resourceDescriptorBindings = compiledBundle.resourceDescriptorSlotMap;

		struct PipelineStateStream {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_CS CS;
		};

		PipelineStateStream pipelineStateStream = {};
		pipelineStateStream.RootSignature = PSOManager::GetInstance().GetRootSignature().Get();
		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
		streamDesc.SizeInBytes = sizeof(PipelineStateStream);
		streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

		auto& device = DeviceManager::GetInstance().GetDevice();
		ID3D12Device2* device2 = nullptr;
		ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_frustrumCullingPSO)));

		//PSOManager::GetInstance().CompileShader(L"shaders/culling.hlsl", L"MeshletOcclusionCullingCSMain", L"cs_6_6", {}, computeShader);

		//pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
		//ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_occlusionCullingPSO)));

		shaderInfoBundle.computeShader = { L"shaders/meshletCulling.hlsl", L"ClearMeshletFrustrumCullingCSMain", L"cs_6_6" };
		compiledBundle = PSOManager::GetInstance().CompileShaders(shaderInfoBundle);
		computeShader = compiledBundle.computeShader;

		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_clearPSO)));
	}

	flecs::query<Components::Light, Components::LightViewInfo, Components::DepthMap> lightQuery;

	ComPtr<ID3D12PipelineState> m_frustrumCullingPSO;
	ComPtr<ID3D12PipelineState> m_clearPSO;

	std::vector<ResourceIdentifier> m_resourceDescriptorBindings;

	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingBitfieldBuffer = nullptr;
	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingIndirectCommandBuffer = nullptr;
	std::shared_ptr<DynamicGloballyIndexedResource> m_primaryCameraMeshletCullingResetIndirectCommandBuffer = nullptr;
	std::shared_ptr<PixelBuffer> m_primaryCameraLinearDepthMap = nullptr;

	bool m_isOccludersPass = false;
	bool m_isRemaindersPass = false;
	bool m_doResets = true;
	bool m_occlusionCullingEnabled = false;

	std::function<uint8_t()> getNumDirectionalLightCascades;
	std::function<bool()> getShadowsEnabled;
};