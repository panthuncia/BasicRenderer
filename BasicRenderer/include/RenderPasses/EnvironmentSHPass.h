#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/EnvironmentManager.h"
#include "Managers/Singletons/ResourceManager.h"
class EnvironmentSHPass : public ComputePass {
public:
	EnvironmentSHPass() {
		rhi::SamplerDesc shSamplerDesc = {};
		shSamplerDesc.minFilter = rhi::Filter::Linear;
		shSamplerDesc.magFilter = rhi::Filter::Linear;
		shSamplerDesc.mipFilter = rhi::MipFilter::Nearest;
		shSamplerDesc.addressU = rhi::AddressMode::Clamp;
		shSamplerDesc.addressV = rhi::AddressMode::Clamp;
		shSamplerDesc.addressW = rhi::AddressMode::Clamp;
		shSamplerDesc.mipLodBias = 0.0f;
		shSamplerDesc.maxAnisotropy = 1;
		shSamplerDesc.borderPreset = rhi::BorderPreset::TransparentBlack;
		shSamplerDesc.minLod = 0.0f;
		shSamplerDesc.maxLod = (std::numeric_limits<float>::max)();

		m_samplerIndex = ResourceManager::GetInstance().CreateIndexedSampler(shSamplerDesc);
	}

	~EnvironmentSHPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		builder->WithShaderResource(Builtin::Environment::WorkingCubemapGroup)
			.WithUnorderedAccess(Builtin::Environment::InfoBuffer);
	}

	void Setup() override {
		CreatePSO();
		
		RegisterUAV(Builtin::Environment::InfoBuffer, 0);
	}

	PassReturn Execute(RenderContext& context) override {
		auto& commandList = context.commandList;

		// Set the descriptor heaps
		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());
		commandList.BindPipeline(m_PSO.GetAPIPipelineState().GetHandle());

		BindResourceDescriptorIndices(commandList, m_PSO.GetResourceDescriptorSlots());

		// Root parameters
		unsigned int miscParams[NumMiscUintRootConstants] = { };
		miscParams[UintRootConstant1] = m_samplerIndex; // Sampler index

		float miscFloatParams[NumMiscFloatRootConstants] = { };

		auto environments = context.environmentManager->GetAndClearEnvironmentsToComputeSH();
		
		for (auto& env : environments) {
			auto cubemapRes = env->GetReflectionCubemapResolution();
			miscParams[UintRootConstant0] = cubemapRes; // Resolution
			miscParams[UintRootConstant2] = env->GetEnvironmentIndex(); // Environment index

			//miscFloatParams[FloatRootConstant0] =  (4.0f * XM_PI / (cubemapRes * cubemapRes * 6)); // Weight

			commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, miscParams);
			commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscFloatRootSignatureIndex, 0, NumMiscFloatRootConstants, miscFloatParams);

			// dispatch over X×Y tiles, Z=6 faces
			unsigned int groupsX = (cubemapRes + 15) / 16;
			unsigned int groupsY = (cubemapRes + 15) / 16;
			unsigned int groupsZ = 6;
			commandList.Dispatch(groupsX, groupsY, groupsZ);
		}
		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:

	void CreatePSO() {
		m_PSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature(),
			L"shaders/SphericalHarmonics.hlsl",
			L"CSMain",
			{},
			"Environment Spherical Harmonics CS");
	}

	unsigned int m_samplerIndex = 0;
	PipelineState m_PSO;
};