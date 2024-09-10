#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

#include "RenderPass.h"
#include "Resource.h"

class RenderGraph {
public:
	void AddPass(RenderPass* pass);
	void Execute(RenderContext& context);
	void Compile(RenderContext& context);
	//void AllocateResources(RenderContext& context);
	void AddResource(std::shared_ptr<Resource> resource);
	std::shared_ptr<Resource> GetResourceByName(const std::string& name);
private:

	std::vector<RenderPass*> passes;
	std::unordered_map<std::string, std::shared_ptr<Resource>> resourcesByName;
};