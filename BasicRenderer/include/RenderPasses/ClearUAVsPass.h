#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/ECSManager.h"
#include "Scene/Components.h"
#include "boost/container_hash/hash.hpp"

struct ClearIndirectDrawCommandUAVPassInputs {
	bool clearBlend;

	friend bool operator==(const ClearIndirectDrawCommandUAVPassInputs&, const ClearIndirectDrawCommandUAVPassInputs&) = default;
};

inline rg::Hash64 HashValue(const ClearIndirectDrawCommandUAVPassInputs& i) {
	std::size_t seed = 0;

	boost::hash_combine(seed, i.clearBlend);
	return seed;
}


class ClearIndirectDrawCommandUAVsPass : public RenderPass {
public:
	ClearIndirectDrawCommandUAVsPass() {}

	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		auto inputs = Inputs<ClearIndirectDrawCommandUAVPassInputs>();
		m_clearBlend = inputs.clearBlend;

		auto ecsWorld = ECSManager::GetInstance().GetWorld();
		auto blendEntity = ECSManager::GetInstance().GetRenderPhaseEntity(Engine::Primary::OITAccumulationPass);
		m_nonBlendQuery = ECSResourceResolver(ecsWorld.query_builder<>()
			.with<Components::IsIndirectArguments>()
			.without<Components::ParticipatesInPass>(blendEntity)
			.build());
		builder->WithCopyDest(ECSResourceResolver(m_nonBlendQuery));
		if (m_clearBlend) {
			m_blendQuery = ECSResourceResolver(ecsWorld.query_builder<>()
				.with<Components::IsIndirectArguments>()
				.with<Components::ParticipatesInPass>(blendEntity)
				.build());
			builder->WithCopyDest(ECSResourceResolver(m_blendQuery));
		}
	}
  
	void Setup() override {

		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::LightViewInfo>().cached().cache_kind(flecs::QueryCacheAll).build();

		m_nonBlendIndirectCommandBuffers = m_nonBlendQuery.ResolveAs<DynamicGloballyIndexedResource>();
		if (m_clearBlend) {
			m_blendIndirectCommandBuffers = m_blendQuery.ResolveAs<DynamicGloballyIndexedResource>();
		}
	}

	PassReturn Execute(RenderContext& context) override {
		// Reset and get the appropriate command list
		auto& commandList = context.commandList;

		auto counterReset = ResourceManager::GetInstance().GetUAVCounterReset();

		// Opaque buffer
		for (auto& res : m_nonBlendIndirectCommandBuffers) {
			auto counterOffset = res->GetResource()->GetUAVCounterOffset();
			auto apiResource = res->GetAPIResource();
			commandList.CopyBufferRegion(apiResource.GetHandle(), counterOffset, counterReset.GetHandle(), 0, sizeof(UINT));
		}

		// Blend buffer
		if (!m_clearBlend) return {};
		for (auto& res : m_blendIndirectCommandBuffers) {
			auto counterOffset = res->GetResource()->GetUAVCounterOffset();
			auto apiResource = res->GetAPIResource();
			commandList.CopyBufferRegion(apiResource.GetHandle(), counterOffset, counterReset.GetHandle(), 0, sizeof(UINT));
		}

		return {};
	}

	void Cleanup() override {

	}

private:
	bool m_clearBlend = false;
	flecs::query<Components::LightViewInfo> lightQuery;
	ComPtr<ID3D12PipelineState> m_PSO;

	ECSResourceResolver m_nonBlendQuery;
	ECSResourceResolver m_blendQuery;

	std::vector<std::shared_ptr<DynamicGloballyIndexedResource>> m_nonBlendIndirectCommandBuffers;
	std::vector<std::shared_ptr<DynamicGloballyIndexedResource>> m_blendIndirectCommandBuffers;
};

class ClearMeshletCullingCommandUAVsPass : public RenderPass {
public:
	ClearMeshletCullingCommandUAVsPass() {}

	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		builder->WithCopyDest(Builtin::IndirectCommandBuffers::MeshletCulling);
	}

	void Setup() override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::LightViewInfo>().cached().cache_kind(flecs::QueryCacheAll).build();

		m_meshletCullingCommandBuffersResolver = m_resourceRegistryView->RequestResolver(Builtin::IndirectCommandBuffers::MeshletCulling);
	}

	PassReturn Execute(RenderContext& context) override {
		// Reset and get the appropriate command list
		auto& commandList = context.commandList;
		auto counterReset = ResourceManager::GetInstance().GetUAVCounterReset();

		// Meshlet frustrum culling buffer
		for (auto& child : m_meshletCullingCommandBuffersResolver->Resolve()) {
			std::shared_ptr<DynamicGloballyIndexedResource> resource = std::dynamic_pointer_cast<DynamicGloballyIndexedResource>(child);
			if (!resource) {
				continue;
			}
			auto counterOffset = resource->GetResource()->GetUAVCounterOffset();
			auto apiResource = resource->GetAPIResource();
			commandList.CopyBufferRegion(apiResource.GetHandle(), counterOffset, counterReset.GetHandle(), 0, sizeof(UINT));
		}

		return {};
	}

	void Cleanup() override {

	}

private:
	flecs::query<Components::LightViewInfo> lightQuery;
	ComPtr<ID3D12PipelineState> m_PSO;

	//ResourceGroup* m_meshletCullingCommandBuffers;
	std::shared_ptr<IResourceResolver> m_meshletCullingCommandBuffersResolver;
};