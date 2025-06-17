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
#include "Managers/ObjectManager.h"

class DeferredRenderPass : public RenderPass {
public:
	DeferredRenderPass() {
		auto& settingsManager = SettingsManager::GetInstance();
		getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
		getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
		getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
		m_gtaoEnabled = settingsManager.getSettingGetter<bool>("enableGTAO")();
		m_clusteredLightingEnabled = settingsManager.getSettingGetter<bool>("enableClusteredLighting")();
	}

	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		builder->WithShaderResource(Builtin::CameraBuffer,
			Builtin::Environment::PrefilteredCubemapsGroup,
			Builtin::Light::ActiveLightIndices,
			Builtin::Light::InfoBuffer,
			Builtin::Light::PointLightCubemapBuffer,
			Builtin::Light::DirectionalLightCascadeBuffer,
			Builtin::Light::SpotLightMatrixBuffer,
			Builtin::Environment::InfoBuffer,
			Builtin::GBuffer::Normals,
			Builtin::GBuffer::Albedo,
			Builtin::GBuffer::Emissive,
			Builtin::GBuffer::MetallicRoughness,
			Builtin::PrimaryCamera::DepthTexture,
			Builtin::Environment::CurrentCubemap,
			Builtin::Shadows::ShadowMaps)
			.WithRenderTarget(Builtin::Color::HDRColorTarget)
			.WithDepthRead(Builtin::PrimaryCamera::DepthTexture);

		if (m_clusteredLightingEnabled) {
			builder->WithShaderResource(Builtin::Light::ClusterBuffer, Builtin::Light::PagesBuffer);
		}

		if (m_gtaoEnabled) {
			builder->WithShaderResource(Builtin::GTAO::OutputAOTerm);
		}
	}

	void Setup(const ResourceRegistryView& resourceRegistryView) override {
		//m_normalMatrixBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::NormalMatrixBuffer)->GetSRVInfo(0).index;
		//m_postSkinningVertexBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PostSkinningVertices)->GetSRVInfo(0).index;
		//m_meshletOffsetBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletOffsets)->GetSRVInfo(0).index;
		//m_meshletVertexIndexBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletVertexIndices)->GetSRVInfo(0).index;
		//m_meshletTriangleBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::MeshResources::MeshletTriangles)->GetSRVInfo(0).index;
		//m_perObjectBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerObjectBuffer)->GetSRVInfo(0).index;
		m_cameraBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::CameraBuffer)->GetSRVInfo(0).index;
		//m_perMeshBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PerMeshBuffer)->GetSRVInfo(0).index;
		
		if (m_clusteredLightingEnabled) {
			m_lightClusterBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::ClusterBuffer)->GetSRVInfo(0).index;
			m_lightPagesBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::PagesBuffer)->GetSRVInfo(0).index;
		}

		m_activeLightIndicesBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::ActiveLightIndices)->GetSRVInfo(0).index;
		m_lightBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::InfoBuffer)->GetSRVInfo(0).index;
		m_pointLightCubemapBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::PointLightCubemapBuffer)->GetSRVInfo(0).index;
		m_spotLightMatrixBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::SpotLightMatrixBuffer)->GetSRVInfo(0).index;
		m_directionalLightCascadeBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Light::DirectionalLightCascadeBuffer)->GetSRVInfo(0).index;

		m_environmentBufferDescriptorIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::Environment::InfoBuffer)->GetSRVInfo(0).index;

		if (m_gtaoEnabled)
		m_aoTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GTAO::OutputAOTerm)->GetSRVInfo(0).index;
		
		m_normalsTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GBuffer::Normals)->GetSRVInfo(0).index;
		m_albedoTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GBuffer::Albedo)->GetSRVInfo(0).index;
		m_metallicRoughnessTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GBuffer::MetallicRoughness)->GetSRVInfo(0).index;
		m_emissiveTextureSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::GBuffer::Emissive)->GetSRVInfo(0).index;

		m_depthBufferSRVIndex = resourceRegistryView.Request<GloballyIndexedResource>(Builtin::PrimaryCamera::DepthTexture)->GetSRVInfo(0).index;
	}

	PassReturn Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::GetInstance();
		auto& commandList = context.commandList;

		ID3D12DescriptorHeap* descriptorHeaps[] = {
			context.textureDescriptorHeap, // The texture descriptor heap
			context.samplerDescriptorHeap, // The sampler descriptor heap
		};

		commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		//CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
		auto rtvHandle = context.pHDRTarget->GetRTVInfo(0).cpuHandle;
		auto& dsvHandle = context.pPrimaryDepthBuffer->GetDSVInfo(0).cpuHandle;
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

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

		//staticBufferIndices[NormalMatrixBufferDescriptorIndex] = m_normalMatrixBufferSRVIndex;
		//staticBufferIndices[PostSkinningVertexBufferDescriptorIndex] = m_postSkinningVertexBufferSRVIndex;
		//staticBufferIndices[MeshletBufferDescriptorIndex] = m_meshletOffsetBufferSRVIndex;
		//staticBufferIndices[MeshletVerticesBufferDescriptorIndex] = m_meshletVertexIndexBufferSRVIndex;
		//staticBufferIndices[MeshletTrianglesBufferDescriptorIndex] = m_meshletTriangleBufferSRVIndex;
		//staticBufferIndices[PerObjectBufferDescriptorIndex] = m_perObjectBufferSRVIndex;
		staticBufferIndices[CameraBufferDescriptorIndex] = m_cameraBufferSRVIndex;
		//staticBufferIndices[PerMeshBufferDescriptorIndex] = m_perMeshBufferSRVIndex;
		staticBufferIndices[AOTextureDescriptorIndex] = m_aoTextureSRVIndex;
		staticBufferIndices[NormalsTextureDescriptorIndex] = m_normalsTextureSRVIndex;
		staticBufferIndices[AlbedoTextureDescriptorIndex] = m_albedoTextureSRVIndex;
		staticBufferIndices[MetallicRoughnessTextureDescriptorIndex] = m_metallicRoughnessTextureSRVIndex;
		staticBufferIndices[EmissiveTextureDescriptorIndex] = m_emissiveTextureSRVIndex;

		staticBufferIndices[ActiveLightIndicesBufferDescriptorIndex] = m_activeLightIndicesBufferSRVIndex;
		staticBufferIndices[LightBufferDescriptorIndex] = m_lightBufferSRVIndex;
		staticBufferIndices[PointLightCubemapBufferDescriptorIndex] = m_pointLightCubemapBufferSRVIndex;
		staticBufferIndices[SpotLightMatrixBufferDescriptorIndex] = m_spotLightMatrixBufferSRVIndex;
		staticBufferIndices[DirectionalLightCascadeBufferDescriptorIndex] = m_directionalLightCascadeBufferSRVIndex;

		staticBufferIndices[EnvironmentBufferDescriptorIndex] = m_environmentBufferDescriptorIndex;

		commandList->SetGraphicsRoot32BitConstants(StaticBufferRootSignatureIndex, NumStaticBufferRootConstants, &staticBufferIndices, 0);

		unsigned int lightClusterInfo[NumLightClusterRootConstants] = {};
		lightClusterInfo[LightClusterBufferDescriptorIndex] = m_lightClusterBufferSRVIndex;
		lightClusterInfo[LightPagesBufferDescriptorIndex] = m_lightPagesBufferSRVIndex;
		commandList->SetGraphicsRoot32BitConstants(LightClusterRootSignatureIndex, NumLightClusterRootConstants, &lightClusterInfo, 0);

		unsigned int misc[NumMiscUintRootConstants] = {};
		misc[0] = m_depthBufferSRVIndex;

		commandList->SetGraphicsRoot32BitConstants(MiscUintRootSignatureIndex, NumMiscUintRootConstants, &misc, 0);

		unsigned int localPSOFlags = 0;
		if (getImageBasedLightingEnabled()) {
			localPSOFlags |= PSOFlags::PSO_IMAGE_BASED_LIGHTING;
		}

		commandList->DrawInstanced(3, 1, 0, 0); // Fullscreen triangle
		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:

	std::function<bool()> getImageBasedLightingEnabled;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<bool()> getShadowsEnabled;

	//int m_normalMatrixBufferSRVIndex = -1;
	//int m_postSkinningVertexBufferSRVIndex = -1;
	//int m_meshletOffsetBufferSRVIndex = -1;
	//int m_meshletVertexIndexBufferSRVIndex = -1;
	//int m_meshletTriangleBufferSRVIndex = -1;
	//int m_perObjectBufferSRVIndex = -1;
	int m_cameraBufferSRVIndex = -1;
	//int m_perMeshBufferSRVIndex = -1;
	int m_lightClusterBufferSRVIndex = -1;
	int m_lightPagesBufferSRVIndex = -1;

	int m_aoTextureSRVIndex = -1;
	int m_normalsTextureSRVIndex = -1;
	int m_albedoTextureSRVIndex = -1;
	int m_metallicRoughnessTextureSRVIndex = -1;
	int m_emissiveTextureSRVIndex = -1;

	int m_depthBufferSRVIndex = -1;

	int m_activeLightIndicesBufferSRVIndex = -1;
	int m_lightBufferSRVIndex = -1;
	int m_pointLightCubemapBufferSRVIndex = -1;
	int m_spotLightMatrixBufferSRVIndex = -1;
	int m_directionalLightCascadeBufferSRVIndex = -1;

	int m_environmentBufferDescriptorIndex = -1;

	bool m_gtaoEnabled = true;
	bool m_clusteredLightingEnabled = true;
};