#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <wrl/client.h>
#include <variant>

#include "RenderPass.h"
#include "ComputePass.h"
#include "ResourceStates.h"

class Resource;

class RenderGraph {
public:
	void AddRenderPass(std::shared_ptr<RenderPass> pass, RenderPassParameters& resources, std::string name = "");
	void AddComputePass(std::shared_ptr<ComputePass> pass, ComputePassParameters& resources, std::string name = "");
	void Update();
	void Execute(RenderContext& context);
	void Compile();
	void Setup();
	//void AllocateResources(RenderContext& context);
	void AddResource(std::shared_ptr<Resource> resource, bool transition = false, ResourceState initialState = ResourceState::UNKNOWN);
	void CreateResource(std::wstring name);
	std::shared_ptr<Resource> GetResourceByName(const std::wstring& name);
	std::shared_ptr<RenderPass> GetRenderPassByName(const std::string& name);
	std::shared_ptr<ComputePass> GetComputePassByName(const std::string& name);
private:
	struct RenderPassAndResources {
		std::shared_ptr<RenderPass> pass;
		RenderPassParameters resources;
	};

	struct ComputePassAndResources {
		std::shared_ptr<ComputePass> pass;
		ComputePassParameters resources;
	};

	struct ResourceTransition {
		ResourceTransition(std::shared_ptr<Resource> pResource, ResourceState fromState, ResourceState toState, ResourceSyncState prevSyncState, ResourceSyncState newSyncState)
			: pResource(pResource), fromState(fromState), toState(toState), prevSyncState(prevSyncState), newSyncState(newSyncState) {
		}
		std::shared_ptr<Resource> pResource;
		ResourceState fromState;
		ResourceState toState;
		ResourceSyncState prevSyncState = ResourceSyncState::NONE;
		ResourceSyncState newSyncState = ResourceSyncState::NONE;
	};

	enum class CommandQueueType {
		Graphics,
		Compute
	};

	struct PassBatch {
		std::vector<RenderPassAndResources> renderPasses;
		std::vector<ComputePassAndResources> computePasses;
		std::unordered_map<std::wstring, ResourceState> resourceStates; // Desired states in this batch
		std::unordered_map<std::wstring, CommandQueueType> transitionQueue; // Queue to transition resources on
		std::vector<ResourceTransition> renderTransitions; // Transitions needed to reach desired states on the render queue
        std::vector<ResourceTransition> computeTransitions; // Transitions needed to reach desired states on the compute queue

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
	};

    enum class PassType {
        Unknown,
        Render,
        Compute
    };

	struct AnyPassAndResources {
		PassType type = PassType::Unknown;
		std::variant<std::monostate, RenderPassAndResources, ComputePassAndResources> pass;

		AnyPassAndResources() = default;

		explicit AnyPassAndResources(RenderPassAndResources const& rp)
			: type(PassType::Render), pass(rp) {}

		explicit AnyPassAndResources(ComputePassAndResources const& cp)
			: type(PassType::Compute), pass(cp) {}
	};

	std::vector<AnyPassAndResources> passes;
	std::unordered_map<std::string, std::shared_ptr<RenderPass>> renderPassesByName;
	std::unordered_map<std::string, std::shared_ptr<ComputePass>> computePassesByName;
	std::unordered_map<std::wstring, std::shared_ptr<Resource>> resourcesByName;
	std::unordered_map<std::wstring, ResourceState> initialResourceStates;
	std::vector<PassBatch> batches;

	std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> m_graphicsCommandAllocators;
	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> m_graphicsTransitionCommandLists;
	std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> m_computeCommandAllocators;
	std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> m_computeTransitionCommandLists;

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> initialTransitionCommandList;
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

	//void ComputeTransitionsForBatch(PassBatch& batch, const std::unordered_map<std::wstring, ResourceState>& previousStates);
    void UpdateDesiredResourceStates(PassBatch& batch, RenderPassAndResources& passAndResources, std::unordered_set<std::wstring>& renderUAVs);
    void UpdateDesiredResourceStates(PassBatch& batch, ComputePassAndResources& passAndResources, std::unordered_set<std::wstring>& computeUAVs);

	void ComputeResourceLoops(const std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, ResourceSyncState>& finalResourceSyncStates);
	bool IsNewBatchNeeded(PassBatch& currentBatch, const RenderPassAndResources& passAndResources, const std::unordered_set<std::wstring>& computeUAVs);
	bool IsNewBatchNeeded(PassBatch& currentBatch, const ComputePassAndResources& passAndResources, const std::unordered_set<std::wstring>& renderUAVs);

    std::vector<ResourceTransition> UpdateFinalResourceStatesAndGatherTransitionsForPass(std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, ResourceSyncState>& finalResourceSyncStates, std::unordered_map<std::wstring, unsigned int>& transitionHistory, std::unordered_map<std::wstring, unsigned int>& producerHistory, ComputePassAndResources& pass, unsigned int batchIndex);
	std::vector<ResourceTransition> UpdateFinalResourceStatesAndGatherTransitionsForPass(std::unordered_map<std::wstring, ResourceState>& finalResourceStates, std::unordered_map<std::wstring, ResourceSyncState>& finalResourceSyncStates, std::unordered_map<std::wstring, unsigned int>& transitionHistory, std::unordered_map<std::wstring, unsigned int>& producerHistory, RenderPassAndResources& pass, unsigned int batchIndex);
	std::pair<int, int> GetBatchesToWaitOn(ComputePassAndResources& pass, const std::unordered_map<std::wstring, unsigned int>& transitionHistory, const std::unordered_map<std::wstring, unsigned int>& producerHistory);
    std::pair<int, int> GetBatchesToWaitOn(RenderPassAndResources& pass, const std::unordered_map<std::wstring, unsigned int>& transitionHistory, const std::unordered_map<std::wstring, unsigned int>& producerHistory);
};