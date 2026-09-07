#pragma once

#include <functional>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/DescriptorHeapManager.h"
#include "Render/RenderContext.h"
#include "Render/OutputTypes.h"
#include "Resources/PixelBuffer.h"

struct ClearVisibilityFrameData {
	rhi::DescriptorHeapHandle textureHeap{}, samplerHeap{};
	std::vector<rhi::UavClearInfo> uintClears, floatClears;
	rhi::DescriptorSlot depth{};
};

class ClearVisibilityBufferPass final
	: public org::TypedRenderGraphPass<ClearVisibilityBufferPass, ClearVisibilityFrameData> {
public:
	ClearVisibilityBufferPass() {
		m_getOutputType = SettingsManager::GetInstance().getSettingGetter<unsigned int>("outputType");
	}

	void Declare(org::PassBuilder& builder) {
		builder.WithUnorderedAccessClear(Builtin::PrimaryCamera::VisibilityTexture,
			Builtin::Surface::BaseColorOpacity,
			Builtin::Surface::NormalRoughness,
			Builtin::Surface::SpecularAo,
			Builtin::Surface::Emissive,
			Builtin::Surface::Motion,
			Builtin::Surface::Payload0,
			Builtin::Surface::Payload1,
			Builtin::Surface::Identity,
			Builtin::DebugVisualization);
		builder.WithDepthStencilClear(Builtin::PrimaryCamera::DepthTexture);
	}

	void Initialize() {
		m_visibilityBuffer = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::PrimaryCamera::VisibilityTexture);
		m_baseColorOpacity = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::BaseColorOpacity);
		m_normalRoughness = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::NormalRoughness);
		m_specularAo = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::SpecularAo);
		m_emissive = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::Emissive);
		m_motion = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::Motion);
		m_payload0 = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::Payload0);
		m_payload1 = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::Payload1);
		m_surfaceIdentity = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::Surface::Identity);
		m_depthTexture = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::PrimaryCamera::DepthTexture);
		m_debugVisualization = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::DebugVisualization);
	}

	ClearVisibilityFrameData Prepare(const org::PassPrepareContext& preparation) {
		(void)preparation; // Clear parameters are entirely captured from setup resources.
		ClearVisibilityFrameData data{};
		data.textureHeap = DescriptorHeapManager::GetInstance().GetSRVDescriptorHeap().GetHandle();
		data.samplerHeap = DescriptorHeapManager::GetInstance().GetSamplerDescriptorHeap().GetHandle();
		auto append = [&](GloballyIndexedResource* resource, bool integer) {
			rhi::UavClearInfo info{};
			info.cpuVisible = resource->GetUAVNonShaderVisibleInfo(0).slot;
			info.shaderVisible = resource->GetUAVShaderVisibleInfo(0).slot;
			info.resource = resource->GetAPIResource();
			(integer ? data.uintClears : data.floatClears).push_back(info);
		};
		append(m_visibilityBuffer, true);
		append(m_surfaceIdentity, true);
		append(m_baseColorOpacity, false);
		append(m_normalRoughness, false);
		append(m_specularAo, false);
		append(m_emissive, false);
		append(m_motion, false);
		append(m_payload0, false);
		append(m_payload1, false);
		append(m_debugVisualization, true);
		data.depth = m_depthTexture->GetDSVInfo(0).slot;
		return data;
	}

	void ShutdownPass() {
		// Cleanup the render pass
	}
	static void Record(const ClearVisibilityFrameData& data, org::PassRecordContext& recording) {
		auto& commands = recording.Commands();
		commands.SetDescriptorHeaps(data.textureHeap, data.samplerHeap);
		rhi::UavClearUint uintValue{};
		uintValue.v[0] = uintValue.v[1] = 0xFFFFFFFF;
		for (const auto& clear : data.uintClears) commands.ClearUavUint(clear, uintValue);
		rhi::UavClearFloat floatValue{};
		for (const auto& clear : data.floatClears) commands.ClearUavFloat(clear, floatValue);
		commands.ClearDepthStencilView(data.depth, true, false, 1.0f, 0);
	}

	private:
	GloballyIndexedResource* m_visibilityBuffer;
	GloballyIndexedResource* m_baseColorOpacity;
	GloballyIndexedResource* m_normalRoughness;
	GloballyIndexedResource* m_specularAo;
	GloballyIndexedResource* m_emissive;
	GloballyIndexedResource* m_motion;
	GloballyIndexedResource* m_payload0;
	GloballyIndexedResource* m_payload1;
	GloballyIndexedResource* m_surfaceIdentity;
	GloballyIndexedResource* m_depthTexture;
	GloballyIndexedResource* m_debugVisualization;
	std::function<unsigned int()> m_getOutputType;
};
