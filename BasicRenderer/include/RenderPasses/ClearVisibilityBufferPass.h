#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/SettingsManager.h"


class ClearVisibilityBufferPass : public ComputePass {
public:
	ClearVisibilityBufferPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		builder->WithUnorderedAccess(Builtin::PrimaryCamera::VisibilityTexture);
	}

	void Setup() override {
		RegisterUAV(Builtin::PrimaryCamera::VisibilityTexture);

		m_visibilityBuffer = m_resourceRegistryView->Request<GloballyIndexedResource>(Builtin::PrimaryCamera::VisibilityTexture);
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

		rhi::UavClearUint clearValue{};
		clearValue.v[0] = 0xFFFFFFFF;
		clearValue.v[1] = 0xFFFFFFFF;

		commandList.ClearUavUint(clearInfo, clearValue);

		return {};
	}

	void Cleanup(RenderContext& context) override {
		// Cleanup the render pass
	}

private:
	std::shared_ptr<GloballyIndexedResource> m_visibilityBuffer;
};