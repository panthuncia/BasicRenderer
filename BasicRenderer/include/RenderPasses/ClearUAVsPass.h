#pragma once

#include <DirectX/d3dx12.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/ECSManager.h"
#include "Scene/Components.h"

class ClearIndirectDrawCommandUAVsPass : public RenderPass {
public:
	ClearIndirectDrawCommandUAVsPass(bool clearBlend) : m_clearBlend(clearBlend) {}

	void DeclareResourceUsages(RenderPassBuilder* builder) override {
		builder->WithCopyDest(Builtin::IndirectCommandBuffers::Opaque, 
			Builtin::IndirectCommandBuffers::AlphaTest);
		if (m_clearBlend) {
			builder->WithCopyDest(Builtin::IndirectCommandBuffers::Blend);
		}
	}
  
	void Setup(const ResourceRegistryView& resourceRegistryView) override {
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();
		lightQuery = ecsWorld.query_builder<Components::LightViewInfo>().cached().cache_kind(flecs::QueryCacheAll).build();

		m_opaqueIndirectCommandBuffer = resourceRegistryView.Request<ResourceGroup>(Builtin::IndirectCommandBuffers::Opaque);
		m_alphaTestIndirectCommandBuffer = resourceRegistryView.Request<ResourceGroup>(Builtin::IndirectCommandBuffers::AlphaTest);
		if (m_clearBlend)
		m_blendIndirectCommandBuffer = resourceRegistryView.Request<ResourceGroup>(Builtin::IndirectCommandBuffers::Blend);
	}

	PassReturn Execute(RenderContext& context) override {
		// Reset and get the appropriate command list
		auto& commandList = context.commandList;

		auto currentScene = context.currentScene;
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();

		auto& objectManager = *context.objectManager;
		auto& meshManager = *context.meshManager;
		auto counterReset = ResourceManager::GetInstance().GetUAVCounterReset();

		// Opaque buffer
		for (auto& child : m_opaqueIndirectCommandBuffer->GetChildren()) {
			std::shared_ptr<DynamicGloballyIndexedResource> resource = std::dynamic_pointer_cast<DynamicGloballyIndexedResource>(child);
			if (!resource) continue;
			auto counterOffset = resource->GetResource()->GetUAVCounterOffset();
			auto apiResource = resource->GetAPIResource();
			commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
		}

		// Alpha test buffer
		for (auto& child : m_alphaTestIndirectCommandBuffer->GetChildren()) {
			std::shared_ptr<DynamicGloballyIndexedResource> resource = std::dynamic_pointer_cast<DynamicGloballyIndexedResource>(child);
			if (!resource) continue;
			auto counterOffset = resource->GetResource()->GetUAVCounterOffset();
			auto apiResource = resource->GetAPIResource();
			commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
		}

		// Blend buffer
		if (!m_clearBlend) return {};
		for (auto& child : m_blendIndirectCommandBuffer->GetChildren()) {
			std::shared_ptr<DynamicGloballyIndexedResource> resource = std::dynamic_pointer_cast<DynamicGloballyIndexedResource>(child);
			if (!resource) continue;
			auto counterOffset = resource->GetResource()->GetUAVCounterOffset();
			auto apiResource = resource->GetAPIResource();
			commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
		}

		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:
	bool m_clearBlend = false;
	flecs::query<Components::LightViewInfo> lightQuery;
	ComPtr<ID3D12PipelineState> m_PSO;

	std::shared_ptr<ResourceGroup> m_opaqueIndirectCommandBuffer;
	std::shared_ptr<ResourceGroup> m_alphaTestIndirectCommandBuffer;
	std::shared_ptr<ResourceGroup> m_blendIndirectCommandBuffer;
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

		m_meshletCullingCommandBuffers = m_resourceRegistryView->Request<ResourceGroup>(Builtin::IndirectCommandBuffers::MeshletCulling);
	}

	PassReturn Execute(RenderContext& context) override {
		// Reset and get the appropriate command list
		auto& commandList = context.commandList;

		auto currentScene = context.currentScene;
		auto& ecsWorld = ECSManager::GetInstance().GetWorld();

		auto& objectManager = *context.objectManager;
		auto& meshManager = *context.meshManager;
		auto counterReset = ResourceManager::GetInstance().GetUAVCounterReset();

		// Meshlet frustrum culling buffer
		for (auto& child : m_meshletCullingCommandBuffers->GetChildren()) {
			std::shared_ptr<DynamicGloballyIndexedResource> resource = std::dynamic_pointer_cast<DynamicGloballyIndexedResource>(child);
			if (!resource) continue;
			auto counterOffset = resource->GetResource()->GetUAVCounterOffset();
			auto apiResource = resource->GetAPIResource();
			commandList->CopyBufferRegion(apiResource, counterOffset, counterReset, 0, sizeof(UINT));
		}

		return {};
	}

	void Cleanup(RenderContext& context) override {

	}

private:
	flecs::query<Components::LightViewInfo> lightQuery;
	ComPtr<ID3D12PipelineState> m_PSO;

	std::shared_ptr<ResourceGroup> m_meshletCullingCommandBuffers;
};