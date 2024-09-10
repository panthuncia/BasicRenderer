#include "RenderGraph.h"
#include "RenderContext.h"

void RenderGraph::Compile(RenderContext& context) {
    // Determine resource lifetimes
    for (auto* pass : passes) {
        for (auto* resource : pass->GetReadResources()) {
            resource->AddReadPass(pass);
        }
        for (auto* resource : pass->GetWriteResources()) {
            resource->AddWritePass(pass);
        }
    }

    //AllocateResources(context);
}

std::shared_ptr<Resource> RenderGraph::GetResourceByName(const std::string& name) {
	return resourcesByName[name];
}