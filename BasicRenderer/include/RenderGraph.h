#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

#include "RenderPass.h"
#include "ResourceStates.h"

class Resource;

class RenderGraph {
public:
	void AddPass(std::shared_ptr<RenderPass> pass, PassParameters& resources);
	void Execute(RenderContext& context);
	void Compile();
	void Setup();
	//void AllocateResources(RenderContext& context);
	void AddResource(std::shared_ptr<Resource> resource);
	void CreateResource(std::wstring name);
	std::shared_ptr<Resource> GetResourceByName(const std::wstring& name);
private:
	struct PassAndResources {
		std::shared_ptr<RenderPass> pass;
		PassParameters resources;
	};

	struct ResourceTransition {
		std::shared_ptr<Resource> pResource;
		ResourceState fromState;
		ResourceState toState;
	};

	struct PassBatch {
		std::vector<PassAndResources> passes;
		std::unordered_map<std::wstring, ResourceState> resourceStates; // Desired states in this batch
		std::vector<ResourceTransition> transitions; // Transitions needed to reach desired states
	};

	std::vector<PassAndResources> passes;
	std::unordered_map<std::wstring, std::shared_ptr<Resource>> resourcesByName;
	std::vector<PassBatch> batches;

	void ComputeTransitionsForBatch(PassBatch& batch, const std::unordered_map<std::wstring, ResourceState>& previousStates);
	void UpdateDesiredResourceStates(PassBatch& batch, PassAndResources& passAndResources);
	void ComputeResourceLoops(const std::unordered_map<std::wstring, ResourceState>& finalResourceStates);
	bool IsNewBatchNeeded(PassBatch& currentBatch, const PassAndResources& passAndResources);
};