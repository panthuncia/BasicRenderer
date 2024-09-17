#include "RenderGraph.h"
#include "RenderContext.h"
#include "utilities.h"

static bool mapHasResourceNotInState(std::unordered_map<std::string, ResourceState>& map, std::string resourceName, ResourceState state) {
    return mapHasKeyNotAsValue<std::string, ResourceState>(map, resourceName, state);
}

void RenderGraph::Compile() {
    batches.clear();
    auto currentBatch = PassBatch();
    std::unordered_map<std::string, ResourceState> previousBatchResourceStates;
    std::unordered_map<std::string, ResourceState> finalResourceStates; // To track final states

    for (auto& passAndResources : passes) {
        bool needsNewBatch = false;

        // Determine if a new batch is needed based on resource state conflicts
        if (IsNewBatchNeeded(currentBatch, passAndResources)) {
            // Compute transitions for the current batch
            ComputeTransitionsForBatch(currentBatch, previousBatchResourceStates);
            batches.push_back(std::move(currentBatch));
            currentBatch = PassBatch();
            previousBatchResourceStates = batches.back().resourceStates;
        }

        currentBatch.passes.push_back(passAndResources);

        // Update desired resource states
        UpdateDesiredResourceStates(currentBatch, passAndResources);

        // Update final resource states
        for (const auto& [resourceName, state] : currentBatch.resourceStates) {
            finalResourceStates[resourceName] = state;
        }
    }

    // Handle the last batch
    ComputeTransitionsForBatch(currentBatch, previousBatchResourceStates);
    batches.push_back(std::move(currentBatch));

    // Insert transitions to loop resources back to their initial states
    ComputeResourceLoops(finalResourceStates);
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

bool RenderGraph::IsNewBatchNeeded(PassBatch& currentBatch, const PassAndResources& passAndResources) {
    for (auto& resource : passAndResources.resources.shaderResources) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::ShaderResource)) {
            return true;
        }
    }
    for (auto& resource : passAndResources.resources.renderTargets) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::RenderTarget)) {
            return true;
            break;
        }
    }
    for (auto& resource : passAndResources.resources.depthTextures) {
        if (mapHasResourceNotInState(currentBatch.resourceStates, resource->GetName(), ResourceState::DepthWrite)) {
            return true;
            break;
        }
    }
}

void RenderGraph::ComputeTransitionsForBatch(PassBatch& batch, const std::unordered_map<std::string, ResourceState>& previousStates) {
    for (const auto& [resourceName, requiredState] : batch.resourceStates) {
        ResourceState previousState = ResourceState::Undefined;
        auto it = previousStates.find(resourceName);
        std::shared_ptr<Resource> resource;
        if (it != previousStates.end()) {
            previousState = it->second;
        }
        else {
            resource = GetResourceByName(resourceName);
            // Use the resource's current state
            if (resource) {
                previousState = resource->GetState();
            }
            else {
                // Handle error: resource not found
                throw std::runtime_error("Resource not found: " + resourceName);
            }
        }
        if (previousState != requiredState) {
            batch.transitions.push_back({ resource, previousState, requiredState });
        }
    }
}

void RenderGraph::UpdateDesiredResourceStates(PassBatch& batch, PassAndResources& passAndResources) {
    // Update batch.resourceStates based on the resources used in passAndResources
    for (auto& resource : passAndResources.resources.shaderResources) {
        batch.resourceStates[resource->GetName()] = ResourceState::ShaderResource;
    }
    for (auto& resource : passAndResources.resources.renderTargets) {
        batch.resourceStates[resource->GetName()] = ResourceState::RenderTarget;
    }
    for (auto& resource : passAndResources.resources.depthTextures) {
        batch.resourceStates[resource->GetName()] = ResourceState::DepthWrite;
    }
}

void RenderGraph::ComputeResourceLoops(const std::unordered_map<std::string, ResourceState>& finalResourceStates) {
    auto& lastBatch = batches.back();

    for (const auto& [resourceName, finalState] : finalResourceStates) {
        auto resource = GetResourceByName(resourceName);
        if (!resource) {
            throw std::runtime_error("Resource not found: " + resourceName);
        }

        ResourceState initialState = resource->GetState();
        if (finalState != initialState) {
            // Insert a transition to bring the resource back to its initial state
            ResourceTransition transition = { resource, finalState, initialState };
            lastBatch.transitions.push_back(transition);
        }
    }
}