#include "RenderGraph.h"
#include "RenderContext.h"
#include "utilities.h"

static bool mapHasResourceNotInState(std::unordered_map<std::string, ResourceState>& map, std::string resourceName, ResourceState state) {
    return mapHasKeyNotAsValue<std::string, ResourceState>(map, resourceName, state);
}

void RenderGraph::Compile() {

    // Determine pass batches and transitions.
    std::vector<PassBatch> batches;
    auto currentBatch = PassBatch();

    // TODO: Decide how to use combined resource states
    for (auto& passAndResources : passes) {
        bool needsNewBatch = false;

        for (auto& resource : passAndResources.resources.shaderResources) {
            if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName() , ResourceState::ShaderResource)) {
                needsNewBatch = true;
                break;
            }
        }

        for (auto& resource : passAndResources.resources.renderTargets) {
            if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName() , ResourceState::RenderTarget)) {
                needsNewBatch = true;
                break;
            }
        }

        for (auto& resource : passAndResources.resources.depthTextures) {
            if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::DepthWrite)) {
                needsNewBatch = true;
                break;
            }
        }

        if (needsNewBatch) {
            batches.push_back(std::move(currentBatch));
            currentBatch = PassBatch();
        }

        currentBatch.passes.push_back(passAndResources);

        // Update resource states based on this pass
        for (auto& resource : passAndResources.resources.shaderResources) {
            currentBatch.resourceStates[resource->GetName()] = ResourceState::ShaderResource;
        }

        for (auto& resource : passAndResources.resources.renderTargets) {
            currentBatch.resourceStates[resource->GetName()] = ResourceState::RenderTarget;
        }

        for (auto& resource : passAndResources.resources.depthTextures) {
            currentBatch.resourceStates[resource->GetName()] = ResourceState::DepthWrite;
        }
    }
}

void RenderGraph::Setup() {
    for (auto& pass : passes) {
        pass.pass->Setup();
    }
}

void RenderGraph::AddPass(std::shared_ptr<RenderPass> pass, PassParameters& resources) {
    PassAndResources passAndResources;
    passAndResources.pass = pass;
    passAndResources.resources = resources;
	passes.push_back(passAndResources);
}

void RenderGraph::AddResource(std::shared_ptr<Resource> resource) {
    auto& name = resource->GetName();
    resourcesByName[name] = resource;
}

std::shared_ptr<Resource> RenderGraph::GetResourceByName(const std::string& name) {
	return resourcesByName[name];
}

void RenderGraph::Execute(RenderContext& context) {
	for (auto& passAndResources : passes) {
		//passAndResources.pass->Setup(context);
		passAndResources.pass->Execute(context);
		//passAndResources.pass->Cleanup(context);
	}
}