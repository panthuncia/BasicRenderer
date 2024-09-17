#include "RenderGraph.h"
#include "RenderContext.h"
#include "utilities.h"

static bool mapHasResourceNotInState(std::unordered_map<std::string, ResourceState>& map, std::string resourceName, ResourceState state) {
    return mapHasKeyNotAsValue<std::string, ResourceState>(map, resourceName, state);
}

void RenderGraph::Compile() {
    // Prepare batches and transitions
    batches.clear(); // Clear any existing batches
    auto currentBatch = PassBatch();
    std::unordered_map<std::string, ResourceState> previousBatchResourceStates;

    for (auto& passAndResources : passes) {
        bool needsNewBatch = false;

        // Check if any resource is in a different state than needed
        for (auto& resource : passAndResources.resources.shaderResources) {
            if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::ShaderResource)) {
                needsNewBatch = true;
                break;
            }
        }
        for (auto& resource : passAndResources.resources.renderTargets) {
            if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::RenderTarget)) {
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
            // Compute transitions for the current batch
            for (const auto& [resourceName, requiredState] : currentBatch.resourceStates) {
                auto resource = GetResourceByName(resourceName);
                ResourceState previousState = ResourceState::Undefined;
                auto it = previousBatchResourceStates.find(resourceName);
                if (it != previousBatchResourceStates.end()) {
                    previousState = it->second;
                }
                if (previousState != requiredState) {
                    currentBatch.transitions.push_back({ resource, previousState, requiredState });
                }
            }

            // Save the current batch and start a new one
            batches.push_back(std::move(currentBatch));
            currentBatch = PassBatch();
            previousBatchResourceStates = batches.back().resourceStates;
        }

        currentBatch.passes.push_back(passAndResources);

        // Update desired resource states
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
    // Handle the last batch
    for (const auto& [resourceName, requiredState] : currentBatch.resourceStates) {
        auto resource = GetResourceByName(resourceName);
        ResourceState previousState = ResourceState::Undefined;
        auto it = previousBatchResourceStates.find(resourceName);
        if (it != previousBatchResourceStates.end()) {
            previousState = it->second;
        }
        if (previousState != requiredState) {
            currentBatch.transitions.push_back({ resource, previousState, requiredState });
        }
    }
    batches.push_back(std::move(currentBatch));
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
    resourcesByName[resource->GetName()] = resource;
}

std::shared_ptr<Resource> RenderGraph::GetResourceByName(const std::string& name) {
	return resourcesByName[name];
}

void RenderGraph::Execute(RenderContext& context) {
    for (auto& batch : batches) {
        // Perform resource transitions
        for (auto& transition : batch.transitions) {
            transition.pResource->Transition(context, transition.fromState, transition.toState);

        }

        // Execute all passes in the batch
        for (auto& passAndResources : batch.passes) {
            passAndResources.pass->Execute(context);
        }
    }
}