#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"

class ClearVisibilityBufferPass : public ComputePass {
public:
	ClearVisibilityBufferPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		builder->WithUnorderedAccess(Builtin::PrimaryCamera::VisibilityTexture,
			Builtin::GBuffer::Albedo,
			Builtin::GBuffer::Emissive,
			Builtin::GBuffer::MetallicRoughness,
			Builtin::GBuffer::Normals,
			Builtin::GBuffer::MotionVectors);
	}

	void Setup() override {
		m_visibilityBuffer = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::PrimaryCamera::VisibilityTexture);
		m_albedo = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GBuffer::Albedo);
		m_metallicRoughness = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GBuffer::MetallicRoughness);
		m_emissive = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GBuffer::Emissive);
		m_normals = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GBuffer::Normals);
		m_motionVectors = m_resourceRegistryView->RequestPtr<GloballyIndexedResource>(Builtin::GBuffer::MotionVectors);
	}

	PassReturn Execute(RenderContext& context) override {
		auto& psoManager = PSOManager::GetInstance();
		auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(),
			context.samplerDescriptorHeap.GetHandle());

		rhi::UavClearInfo clearInfo{};
		clearInfo.cpuVisible = m_visibilityBuffer->GetUAVNonShaderVisibleInfo(0).slot;
		clearInfo.shaderVisible = m_visibilityBuffer->GetUAVShaderVisibleInfo(0).slot;
		clearInfo.resource = m_visibilityBuffer->GetAPIResource();

		// Visibility buffer clear value: 0xFFFFFFFF, 0xFFFFFFFF
		rhi::UavClearUint clearValue{};
		clearValue.v[0] = 0xFFFFFFFF;
		clearValue.v[1] = 0xFFFFFFFF;

		commandList.ClearUavUint(clearInfo, clearValue);

		// Everything else: 0
		rhi::UavClearFloat clearValueFloat{};
		clearValueFloat.v[0] = 0;
		clearValueFloat.v[1] = 0;

		auto clearResource = [&](GloballyIndexedResource* resource) {
			if (resource) {
				rhi::UavClearInfo info{};
				info.cpuVisible = resource->GetUAVNonShaderVisibleInfo(0).slot;
				info.shaderVisible = resource->GetUAVShaderVisibleInfo(0).slot;
				info.resource = resource->GetAPIResource();
				commandList.ClearUavFloat(info, clearValueFloat);
			}
			};

		clearResource(m_albedo);
		clearResource(m_metallicRoughness);
		clearResource(m_emissive);
		clearResource(m_normals);
		clearResource(m_motionVectors);

		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	GloballyIndexedResource* m_visibilityBuffer;
	GloballyIndexedResource* m_albedo;
	GloballyIndexedResource* m_metallicRoughness;
	GloballyIndexedResource* m_emissive;
	GloballyIndexedResource* m_normals;
	GloballyIndexedResource* m_motionVectors;
};