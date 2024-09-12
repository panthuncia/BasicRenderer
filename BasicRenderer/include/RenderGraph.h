#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

#include "RenderPass.h"
#include "Resource.h"

class RenderGraph {
public:
	void AddPass(std::shared_ptr<RenderPass> pass, PassParameters& resources);
	void Execute(RenderContext& context);
	void Compile();
	//void AllocateResources(RenderContext& context);
	void AddResource(std::shared_ptr<Resource> resource);
	std::shared_ptr<Resource> GetResourceByName(const std::string& name);
private:
	struct PassAndResources {
		std::shared_ptr<RenderPass> pass;
		PassParameters resources;
	};

	struct PassBatch {
		std::vector<PassAndResources> passes;
		std::unordered_map<std::string, ResourceState> resourceStates;
	};

	std::vector<PassAndResources> passes;
	std::unordered_map<std::string, std::shared_ptr<Resource>> resourcesByName;
};