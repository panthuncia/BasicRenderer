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

class SkinningPass : public ComputePass {
public:
	SkinningPass() {
		getMeshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader");
	}

	~SkinningPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) {
		builder->WithShaderResource(Builtin::PerObjectBuffer, Builtin::PerMeshBuffer, Builtin::PerMeshInstanceBuffer, Builtin::PreSkinningVertices, Builtin::NormalMatrixBuffer)
			.WithUnorderedAccess(Builtin::PostSkinningVertices);
	}

	void Setup(const ResourceRegistryView& resourceRegistryView) override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		opaqueQuery = ecsWorld.query_builder<Components::OpaqueSkinned, Components::ObjectDrawInfo, Components::OpaqueMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		alphaTestQuery = ecsWorld.query_builder<Components::AlphaTestSkinned, Components::ObjectDrawInfo, Components::AlphaTestMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		blendQuery = ecsWorld.query_builder<Components::BlendSkinned, Components::ObjectDrawInfo, Components::BlendMeshInstances>().cached().cache_kind(flecs::QueryCacheAll).build();
		CreatePSO();

		m_preSkinningVertexBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PreSkinningVertices)->GetSRVInfo(0).index;
		m_normalMatrixBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::NormalMatrixBuffer)->GetSRVInfo(0).index;
		m_postSkinningVertexBufferUAVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PostSkinningVertices)->GetUAVShaderVisibleInfo(0).index;
		m_perObjectBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerObjectBuffer)->GetSRVInfo(0).index;
		m_perMeshInstanceBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshInstanceBuffer)->GetSRVInfo(0).index;
		m_perMeshBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshBuffer)->GetSRVInfo(0).index;
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

		unsigned int staticBufferIndices[NumStaticBufferRootConstants] = {};
		staticBufferIndices[PreSkinningVertexBufferDescriptorIndex] = m_preSkinningVertexBufferSRVIndex;
		staticBufferIndices[NormalMatrixBufferDescriptorIndex] = m_normalMatrixBufferSRVIndex;
		staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = m_postSkinningVertexBufferUAVIndex;
		staticBufferIndices[PerObjectBufferDescriptorIndex] = m_perObjectBufferSRVIndex;
		staticBufferIndices[PerMeshInstanceBufferDescriptorIndex] = m_perMeshInstanceBufferSRVIndex;
		staticBufferIndices[PerMeshBufferDescriptorIndex] = m_perMeshBufferSRVIndex;

		commandList->SetComputeRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, staticBufferIndices, 0);

		auto meshShadersEnabled = getMeshShadersEnabled();

		unsigned int perMeshConstants[NumPerMeshRootConstants] = {};
		opaqueQuery.each([&](flecs::entity e, Components::OpaqueSkinned s, Components::ObjectDrawInfo drawInfo, Components::OpaqueMeshInstances meshInstances) {
			auto& meshes = meshInstances.meshInstances;

			commandList->SetComputeRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				perMeshConstants[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				perMeshConstants[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
				commandList->SetComputeRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, &perMeshConstants, PerMeshBufferIndex);
				
				unsigned int numGroups = std::ceil(mesh.GetNumVertices(meshShadersEnabled) / 64.0);
				commandList->Dispatch(numGroups, 1, 1);
			}
			});

		alphaTestQuery.each([&](flecs::entity e, Components::AlphaTestSkinned s, Components::ObjectDrawInfo drawInfo, Components::AlphaTestMeshInstances meshInstances) {
			auto& meshes = meshInstances.meshInstances;

			commandList->SetComputeRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				perMeshConstants[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				perMeshConstants[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
				commandList->SetComputeRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, &perMeshConstants, PerMeshBufferIndex);

				unsigned int numGroups = std::ceil(mesh.GetNumVertices(meshShadersEnabled) / 64.0);
				commandList->Dispatch(numGroups, 1, 1);
			}
			});

		blendQuery.each([&](flecs::entity e, Components::BlendSkinned s, Components::ObjectDrawInfo drawInfo, Components::BlendMeshInstances meshInstances) {
			auto& meshes = meshInstances.meshInstances;

			commandList->SetComputeRoot32BitConstants(PerObjectRootSignatureIndex, 1, &drawInfo.perObjectCBIndex, PerObjectBufferIndex);

			for (auto& pMesh : meshes) {
				auto& mesh = *pMesh->GetMesh();
				perMeshConstants[PerMeshBufferIndex] = mesh.GetPerMeshBufferView()->GetOffset() / sizeof(PerMeshCB);
				perMeshConstants[PerMeshInstanceBufferIndex] = pMesh->GetPerMeshInstanceBufferOffset() / sizeof(PerMeshInstanceCB);
				commandList->SetComputeRoot32BitConstants(PerMeshRootSignatureIndex, NumPerMeshRootConstants, &perMeshConstants, PerMeshBufferIndex);

				unsigned int numGroups = std::ceil(mesh.GetNumVertices(meshShadersEnabled) / 64.0);
				commandList->Dispatch(numGroups, 1, 1);
			}
			});
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	int m_preSkinningVertexBufferSRVIndex = -1;
	int m_normalMatrixBufferSRVIndex = -1;
	int m_postSkinningVertexBufferUAVIndex = -1;
	int m_perObjectBufferSRVIndex = -1;
	int m_perMeshInstanceBufferSRVIndex = -1;
	int m_perMeshBufferSRVIndex = -1;

	void CreatePSO() {
		// Compile the compute shader
		Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
		PSOManager::GetInstance().CompileShader(L"shaders/skinning.brsl", L"CSMain", L"cs_6_6", {}, computeShader);

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

	flecs::query<Components::OpaqueSkinned, Components::ObjectDrawInfo, Components::OpaqueMeshInstances> opaqueQuery;
	flecs::query<Components::AlphaTestSkinned, Components::ObjectDrawInfo, Components::AlphaTestMeshInstances> alphaTestQuery;
	flecs::query<Components::BlendSkinned, Components::ObjectDrawInfo, Components::BlendMeshInstances> blendQuery;
	ComPtr<ID3D12PipelineState> m_PSO;

	std::function<bool()> getMeshShadersEnabled;
};