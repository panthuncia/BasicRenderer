#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/MeshManager.h"
#include "Managers/ObjectManager.h"
#include "Managers/Singletons/ECSManager.h"

class LightCullingPass : public ComputePass {
public:
	LightCullingPass() {
		getClusterSize = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT3>("lightClusterSize");
	}

	~LightCullingPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		builder->WithShaderResource(Builtin::CameraBuffer, Builtin::Light::ActiveLightIndices, Builtin::Light::InfoBuffer)
			.WithUnorderedAccess(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer, Builtin::Light::PagesCounter);
	}

	void Setup(const ResourceRegistryView& resourceRegistryView) override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		CreatePSO();

		m_pLightPagesCounter = resourceRegistryView.Request<Buffer>(Builtin::Light::PagesCounter);
		m_cameraBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).index;
		m_lightClusterBufferUAVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::ClusterBuffer)->GetUAVShaderVisibleInfo(0).index;
		m_lightPagesBufferUAVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::PagesBuffer)->GetUAVShaderVisibleInfo(0).index;
		m_activeLightIndicesBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::ActiveLightIndices)->GetSRVInfo(0).index;
		m_lightInfoBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::InfoBuffer)->GetSRVInfo(0).index;
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		ID3D12DescriptorHeap* descriptorHeaps[] = {
			ResourceManager::GetInstance().GetSRVDescriptorHeap().Get(),
			ResourceManager::GetInstance().GetSamplerDescriptorHeap().Get()
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		auto rootSignature = PSOManager::GetInstance().GetComputeRootSignature().Get();
		commandList->SetComputeRootSignature(rootSignature);

		// Set the compute pipeline state
		commandList->SetPipelineState(m_PSO.Get());

		auto& meshManager = context.meshManager;
		auto& objectManager = context.objectManager;
		auto& cameraManager = context.cameraManager;

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[CameraBufferDescriptorIndex] = m_cameraBufferSRVIndex;
		staticBufferIndices[ActiveLightIndicesBufferDescriptorIndex] = m_activeLightIndicesBufferSRVIndex;
		staticBufferIndices[LightBufferDescriptorIndex] = m_lightInfoBufferSRVIndex;
		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, staticBufferIndices, 0);

		unsigned int lightClusterConstants[NumLightClusterRootConstants] = {};
		lightClusterConstants[LightClusterBufferDescriptorIndex] = m_lightClusterBufferUAVIndex;
		lightClusterConstants[LightPagesBufferDescriptorIndex] = m_lightPagesBufferUAVIndex;
		lightClusterConstants[LightPagesCounterDescriptorIndex] = m_pLightPagesCounter->GetUAVShaderVisibleInfo(0).index;
		lightClusterConstants[LightPagesPoolSize] = context.lightManager->GetLightPagePoolSize();
		commandList->SetComputeRoot32BitConstants(LightClusterRootSignatureIndex, NumLightClusterRootConstants, lightClusterConstants, 0);

		auto clusterSize = getClusterSize();
		unsigned int numThreadGroups = std::ceil(((float)(clusterSize.x * clusterSize.y * clusterSize.z)) / 128);
		commandList->Dispatch(numThreadGroups, 1, 1);
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

	virtual void Update() override {
		// Reset UAV counter
		uint32_t zero = 0;
		UploadManager::GetInstance().UploadData(&zero, sizeof(uint32_t), m_pLightPagesCounter.get(), 0);
	}

private:

	std::shared_ptr<Buffer> m_pLightPagesCounter = nullptr;
	int m_cameraBufferSRVIndex = -1;
	int m_lightClusterBufferUAVIndex = -1;
	int m_lightPagesBufferUAVIndex = -1;
	int m_activeLightIndicesBufferSRVIndex = -1;
	int m_lightInfoBufferSRVIndex = -1;

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		PSOManager::GetInstance().CompileShader(L"shaders/lightCulling.hlsl", L"CSMain", L"cs_6_6", {}, computeShader);

		struct PipelineStateStream {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_CS CS;
		};

		PipelineStateStream pipelineStateStream = {};
		pipelineStateStream.RootSignature = PSOManager::GetInstance().GetComputeRootSignature().Get();
		pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
		streamDesc.SizeInBytes = sizeof(PipelineStateStream);
		streamDesc.pPipelineStateSubobjectStream = &pipelineStateStream;

		auto& device = DeviceManager::GetInstance().GetDevice();
		ID3D12Device2* device2 = nullptr;
		ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&device2)));
		ThrowIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_PSO)));
	}

	std::function<DirectX::XMUINT3()> getClusterSize;
	ComPtr<ID3D12PipelineState> m_PSO;
};