#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/EnvironmentManager.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Interfaces/IDynamicDeclaredResources.h"

#include <vector>

class EnvironmentSHPass : public ComputePass, public IDynamicDeclaredResources {
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

		CreatePSO();
	}

	~EnvironmentSHPass() {
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		for (const auto& j : m_pending) {
			if (!j.srcCubemap) continue;
			builder->WithShaderResource(j.srcCubemap);
		}

		builder->WithUnorderedAccess(Builtin::Environment::InfoBuffer);

		m_declaredResourcesChanged = false;
	}

	void Setup() override {
		RegisterUAV(Builtin::Environment::InfoBuffer, 0);
	}

	void Update(const UpdateExecutionContext& context) override {
		std::vector<Job> newPending;
		auto* updateData = context.hostData ? const_cast<RendererUpdateData*>(context.hostData->Get<RendererUpdateData>()) : nullptr;

		if (updateData && updateData->environmentManager) {
			auto environments = updateData->environmentManager->GetAndClearEnvironmentsToComputeSH();
			newPending.reserve(environments.size());

			for (auto* env : environments) {
				if (!env) continue;

				auto srcCubeAsset = env->GetEnvironmentCubemap();
				if (!srcCubeAsset) continue;

				auto srcCube = srcCubeAsset->ImagePtr();
				if (!srcCube) continue;

				Job j{};
				j.srcCubemap = srcCube;
				j.environmentIndex = env->GetEnvironmentIndex();
				j.cubemapResolution = env->GetReflectionCubemapResolution();
				newPending.push_back(std::move(j));
			}
		}

		auto sameJobs = [](const std::vector<Job>& a, const std::vector<Job>& b) {
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i) {
				if (a[i].srcCubemap.get() != b[i].srcCubemap.get()) return false;
				if (a[i].environmentIndex != b[i].environmentIndex) return false;
				if (a[i].cubemapResolution != b[i].cubemapResolution) return false;
			}
			return true;
		};

		if (!sameJobs(m_pending, newPending)) {
			m_declaredResourcesChanged = true;
			m_pending = std::move(newPending);
		}
	}

	PassReturn Execute(PassExecutionContext& executionContext) override {
		auto* renderContext = executionContext.hostData ? const_cast<RenderContext*>(executionContext.hostData->Get<RenderContext>()) : nullptr;
		if (!renderContext) return {};
		auto& context = *renderContext;
		if (m_pending.empty()) return {};

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

		for (const auto& j : m_pending) {
			if (!j.srcCubemap) continue;

			auto cubemapRes = j.cubemapResolution;
			miscParams[UintRootConstant0] = cubemapRes; // Resolution
			miscParams[UintRootConstant2] = j.environmentIndex; // Environment index

			//miscFloatParams[FloatRootConstant0] =  (4.0f * XM_PI / (cubemapRes * cubemapRes * 6)); // Weight

			commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, miscParams);
			commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscFloatRootSignatureIndex, 0, NumMiscFloatRootConstants, miscFloatParams);

			// dispatch over X�Y tiles, Z=6 faces
			unsigned int groupsX = (cubemapRes + 15) / 16;
			unsigned int groupsY = (cubemapRes + 15) / 16;
			unsigned int groupsZ = 6;
			commandList.Dispatch(groupsX, groupsY, groupsZ);
		}

		m_declaredResourcesChanged = true;
		m_pending.clear();

		return {};
	}

	bool DeclaredResourcesChanged() const override {
		return m_declaredResourcesChanged;
	}

	void Cleanup() override {

	}

private:
	struct Job {
		std::shared_ptr<PixelBuffer> srcCubemap;
		uint32_t environmentIndex = 0;
		uint32_t cubemapResolution = 0;
	};

	std::vector<Job> m_pending;
	bool m_declaredResourcesChanged = true;

	void CreatePSO() {
		m_PSO = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
			L"shaders/SphericalHarmonics.hlsl",
			L"CSMain",
			{},
			"Environment Spherical Harmonics CS");
	}

	unsigned int m_samplerIndex = 0;
	PipelineState m_PSO;
};