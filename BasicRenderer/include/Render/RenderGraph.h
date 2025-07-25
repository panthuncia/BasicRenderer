#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <wrl/client.h>
#include <variant>
#include <span>
#include <spdlog/spdlog.h>

#include "RenderPasses/Base/RenderPass.h"
#include "RenderPasses/Base/ComputePass.h"
#include "Resources/ResourceStates.h"
#include "Resources/ResourceStateTracker.h"
#include "Interfaces/IResourceProvider.h"
#include "Render/ResourceRegistry.h"

class Resource;
class RenderPassBuilder;
class ComputePassBuilder;

template<typename T>
concept DerivedResource = std::derived_from<T, Resource>;

class RenderGraph {
public:
	void AddRenderPass(std::shared_ptr<RenderPass> pass, RenderPassParameters& resources, std::string name = "");
	void AddComputePass(std::shared_ptr<ComputePass> pass, ComputePassParameters& resources, std::string name = "");
	void Update();
	void Execute(RenderContext& context);
	void Compile();
	void Setup();
	//void AllocateResources(RenderContext& context);
	//void CreateResource(std::wstring name);
	std::shared_ptr<Resource> GetResourceByName(const std::wstring& name);
	std::shared_ptr<Resource> GetResourceByID(const uint64_t id);
	std::shared_ptr<RenderPass> GetRenderPassByName(const std::string& name);
	std::shared_ptr<ComputePass> GetComputePassByName(const std::string& name);

	void RegisterProvider(IResourceProvider* prov);
	void RegisterResource(ResourceIdentifier id, std::shared_ptr<Resource> resource, IResourceProvider* provider = nullptr);
	std::shared_ptr<Resource> RequestResource(ResourceIdentifier const& rid, bool allowFailure = false);

	template<DerivedResource T>
	std::shared_ptr<T> RequestResource(ResourceIdentifier const& rid, bool allowFailure = false) {
		auto basePtr = RequestResource(rid, allowFailure);

		if (!basePtr) {
			if (allowFailure) {
				return nullptr;
			}

			throw std::runtime_error(
				"RequestResource<" + std::string(typeid(T).name()) +
				">: underlying Resource* is null (rid = " + rid.ToString() + ")"
			);
		}

		auto derivedPtr = std::dynamic_pointer_cast<T>(basePtr);
		if (!derivedPtr) {
			throw std::runtime_error(
				"Requested resource is not a " + std::string(typeid(T).name()) +
				": " + rid.ToString()
			);
		}

		return derivedPtr;
	}

	ComputePassBuilder BuildComputePass(std::string const& name);
	RenderPassBuilder BuildRenderPass(std::string const& name);

private:
	struct RenderPassAndResources {
		std::shared_ptr<RenderPass> pass;
		RenderPassParameters resources;
		std::string name;
		int statisticsIndex = -1;
	};

	struct ComputePassAndResources {
		std::shared_ptr<ComputePass> pass;
		ComputePassParameters resources;
		std::string name;
		int statisticsIndex = -1;
	};

	enum class CommandQueueType {
		Graphics,
		Compute
	};

	struct PassBatch {
		std::vector<RenderPassAndResources> renderPasses;
		std::vector<ComputePassAndResources> computePasses;
		//std::unordered_map<uint64_t, ResourceAccessType> resourceAccessTypes; // Desired access types in this batch
		//std::unordered_map<uint64_t, ResourceLayout> resourceLayouts; // Desired layouts in this batch
		std::unordered_map<uint64_t, CommandQueueType> transitionQueue; // Queue to transition resources on
		std::vector<ResourceTransition> renderTransitions; // Transitions needed to reach desired states on the render queue
        std::vector<ResourceTransition> computeTransitions; // Transitions needed to reach desired states on the compute queue
		std::vector<ResourceTransition> passEndTransitions; // A special case to deal with resources that need to be used by the compute queue, but are in graphics-queue-only states

		// Resources that passes in this batch transition internally
		// Cannot be batched with other passes which use these resources
		// Ideally, would be tracked per-subresource, but that sounds hard to implement
		std::unordered_set<uint64_t> internallyTransitionedResources;
		std::unordered_set<uint64_t> allResources; // All resources used in this batch, including those that are not transitioned internally

		// For each queue, we need to allow a fence to wait on before transitioning, in case a previous batch is still using a resource
		// Also, we need to allow a separate fence to wait on before *executing* the batch, in case the compute and render queue use the same resource in this batch
		bool renderQueueWaitOnComputeQueueBeforeTransition = false;
		UINT64 renderQueueWaitOnComputeQueueBeforeTransitionFenceValue = 0;
		bool renderQueueWaitOnComputeQueueBeforeExecution = false;
		UINT64 renderQueueWaitOnComputeQueueBeforeExecutionFenceValue = 0;

		bool computeQueueWaitOnRenderQueueBeforeTransition = false;
		UINT64 computeQueueWaitOnRenderQueueBeforeTransitionFenceValue = 0;
		bool computeQueueWaitOnRenderQueueBeforeExecution = false;
		UINT64 computeQueueWaitOnRenderQueueBeforeExecutionFenceValue = 0;

		// Fences to signal, after transition and after completion, for each queue
		bool renderTransitionSignal = false;
		UINT64 renderTransitionFenceValue = 0;
		bool computeTransitionSignal = false;
		UINT64 computeTransitionFenceValue = 0;

		bool renderCompletionSignal = false;
		UINT64 renderCompletionFenceValue = 0;
		bool computeCompletionSignal = false;
		UINT64 computeCompletionFenceValue = 0;

		std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> renderCommandLists;
		std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> computeCommandLists;

		std::unordered_map<uint64_t, SymbolicTracker*> passBatchTrackers; // Trackers for the resources in this batch
	};

    enum class PassType {
        Unknown,
        Render,
        Compute
    };

	struct AnyPassAndResources {
		PassType type = PassType::Unknown;
		std::variant<std::monostate, RenderPassAndResources, ComputePassAndResources> pass;
		std::string name;

		AnyPassAndResources() = default;

		explicit AnyPassAndResources(RenderPassAndResources const& rp)
			: type(PassType::Render), pass(rp) {}

		explicit AnyPassAndResources(ComputePassAndResources const& cp)
			: type(PassType::Compute), pass(cp) {}
	};

	struct CompileContext {
		std::unordered_map<uint64_t, unsigned int> usageHistCompute;
		std::unordered_map<uint64_t, unsigned int> usageHistRender;
	};

	std::vector<IResourceProvider*> _providers;
	ResourceRegistry _registry;
	std::unordered_map<ResourceIdentifier, IResourceProvider*, ResourceIdentifier::Hasher> _providerMap;

	using BuilderVariant = std::variant<RenderPassBuilder, ComputePassBuilder>;
	std::vector<BuilderVariant>   m_passBuilders;

	std::vector<AnyPassAndResources> passes;
	std::unordered_map<std::string, std::shared_ptr<RenderPass>> renderPassesByName;
	std::unordered_map<std::string, std::shared_ptr<ComputePass>> computePassesByName;
	std::unordered_map<std::wstring, std::shared_ptr<Resource>> resourcesByName;
	std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
	std::unordered_map<uint64_t, uint64_t> independantlyManagedResourceToGroup;
	std::vector<std::shared_ptr<ResourceGroup>> resourceGroups;

	std::unordered_map<uint64_t, std::unordered_set<uint64_t>> aliasedResources; // Tracks resources that use the same memory
	std::unordered_map<uint64_t, size_t> resourceToAliasGroup;
	std::vector<std::vector<uint64_t>>   aliasGroups;
	std::vector<std::unordered_map<UINT,uint64_t>> lastActiveSubresourceInAliasGroup;

	// Sometimes, we have a resource group that has children that are also managed independently by this graph. If so, we need to handle their transitions separately
	std::unordered_map<uint64_t, std::vector<uint64_t>> resourcesFromGroupToManageIndependantly;

	std::unordered_map<uint64_t, ResourceTransition> initialTransitions; // Transitions needed to reach the initial state of the resources before executing the first batch. Executed on graph setup.
	std::vector<PassBatch> batches;
	std::unordered_map<uint64_t, SymbolicTracker*> trackers; // Tracks the state of resources in the graph.

	std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> m_graphicsCommandAllocators;
	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> m_graphicsCommandLists;
	//std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> m_graphicsBatchEndTransitionCommandLists;
	std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> m_computeCommandAllocators;
	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> m_computeCommandLists;

	//Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> initialTransitionCommandList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> initialTransitionCommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_initialTransitionFence;
	UINT64 m_initialTransitionFenceValue = 0;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_frameStartSyncFence; // TODO: Is there a better way of handling waiting for pre-frame things like copying resources?

	Microsoft::WRL::ComPtr<ID3D12Fence> m_graphicsQueueFence;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_computeQueueFence;

	UINT64 m_graphicsQueueFenceValue = 0;
	UINT64 GetNextGraphicsQueueFenceValue() {
		return m_graphicsQueueFenceValue++;
	}
	UINT64 m_computeQueueFenceValue = 0;
	UINT64 GetNextComputeQueueFenceValue() {
		return m_computeQueueFenceValue++;
	}

	void AddResource(std::shared_ptr<Resource> resource, bool transition = false);

	void ComputeResourceLoops();
	bool IsNewBatchNeeded(
		const std::vector<ResourceRequirement>& reqs,
		const std::vector<std::pair<ResourceAndRange, ResourceState>> passInternalTransitions,
		const std::unordered_map<uint64_t, SymbolicTracker*>& passBatchTrackers,
		const std::unordered_set<uint64_t>& currentBatchInternallyTransitionedResources,
		const std::unordered_set<uint64_t>& currentBatchAllResources,
		const std::unordered_set<uint64_t>& otherQueueUAVs);
	

	std::pair<int, int> GetBatchesToWaitOn(const ComputePassAndResources& pass, const std::unordered_map<uint64_t, unsigned int>& transitionHistory, const std::unordered_map<uint64_t, unsigned int>& producerHistory);
    std::pair<int, int> GetBatchesToWaitOn(const RenderPassAndResources& pass, const std::unordered_map<uint64_t, unsigned int>& transitionHistory, const std::unordered_map<uint64_t, unsigned int>& producerHistory);

	void ProcessResourceRequirements(
		bool isCompute,
		std::vector<ResourceRequirement>& resourceRequirements,
		std::unordered_map<uint64_t, unsigned int>&  batchOfLastRenderQueueUsage,
		std::unordered_map<uint64_t, unsigned int>& producerHistory,
		unsigned int batchIndex,
		PassBatch& currentBatch);

	template<typename PassRes>
	void applySynchronization(
		bool                              isComputePass,
		PassBatch&                        currentBatch,
		unsigned int                      currentBatchIndex,
		const PassRes&                    pass, // either ComputePassAndResources or RenderPassAndResources
		const std::unordered_map<uint64_t, unsigned int>& oppTransHist,
		const std::unordered_map<uint64_t, unsigned int>& oppProdHist)
	{
		// figure out which two numbers we wait on
		auto [lastTransBatch, lastProdBatch] =
			GetBatchesToWaitOn(pass, oppTransHist, oppProdHist);

		// handle the "transition" wait
		if (lastTransBatch != -1) {
			if (lastTransBatch == currentBatchIndex) {
				// same batch, signal & immediate wait
				if (isComputePass) {
					currentBatch.renderTransitionSignal = true;
					currentBatch.computeQueueWaitOnRenderQueueBeforeExecution = true;
					currentBatch.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue = 
						currentBatch.renderTransitionFenceValue;
				} else {
					currentBatch.computeTransitionSignal = true;
					currentBatch.renderQueueWaitOnComputeQueueBeforeExecution = true;
					currentBatch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue = 
						currentBatch.computeTransitionFenceValue;
				}
			} else {
				// different batch, signal that batch's completion, then wait before *transition*
				if (isComputePass) {
					batches[lastTransBatch].renderCompletionSignal = true;
					currentBatch.computeQueueWaitOnRenderQueueBeforeTransition = true;
					currentBatch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue =
						std::max(currentBatch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue,
							batches[lastTransBatch].renderCompletionFenceValue);
				} else {
					batches[lastTransBatch].computeCompletionSignal = true;
					currentBatch.renderQueueWaitOnComputeQueueBeforeTransition = true;
					currentBatch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue =
						std::max(currentBatch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue,
							batches[lastTransBatch].computeCompletionFenceValue);
				}
			}
		}

		// handle the "producer" wait
#if defined(_DEBUG)
		if (lastProdBatch == currentBatchIndex) {
			spdlog::error("Producer batch is the same as current batch");
			__debugbreak();
		}
#endif
		if (lastProdBatch != -1) {
			if (isComputePass) {
				batches[lastProdBatch].renderCompletionSignal = true;
				currentBatch.computeQueueWaitOnRenderQueueBeforeTransition = true;
				currentBatch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue =
					std::max(currentBatch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue,
						batches[lastProdBatch].renderCompletionFenceValue);
			} else {
				batches[lastProdBatch].computeCompletionSignal = true;
				currentBatch.renderQueueWaitOnComputeQueueBeforeTransition = true;
				currentBatch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue =
					std::max(currentBatch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue,
						batches[lastProdBatch].computeCompletionFenceValue);
			}
		}
	}

	void AddTransition(
		std::unordered_map<uint64_t, unsigned int>&  batchOfLastRenderQueueUsage,
		unsigned int batchIndex,
		PassBatch& currentBatch,
		bool isComputePass,
		const ResourceRequirement& r);

	std::vector<uint64_t> GetAllAliasIDs(uint64_t id) const {
		auto it = resourceToAliasGroup.find(id);
		if (it == resourceToAliasGroup.end()) {
			// not aliased
			return { id };
		}
		int group = it->second;
		std::vector<uint64_t> out;
		// scan for any resource mapped to the same group
		for (auto const& p : resourceToAliasGroup) {
			if (p.second == group)
				out.push_back(p.first);
		}
		return out;
	}

	void RegisterPassBuilder(RenderPassBuilder&& builder);
	void RegisterPassBuilder(ComputePassBuilder&& builder);

	friend class RenderPassBuilder;
	friend class ComputePassBuilder;
};