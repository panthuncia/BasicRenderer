#include "Render/RenderGraphBuilder.h"

#include "Render/RenderGraph.h"

std::shared_ptr<Resource> RenderGraphBuilder::RequestResource(ResourceIdentifier const& rid) {
    // If it's already in our registry, return it
    auto it = _registry.find(rid);
    if (it != _registry.end()) 
        return it->second;

	// No provider registered for this key
	std::string_view name = rid.IsBuiltin() ? BuiltinResourceToString(rid.AsBuiltin()) : rid.AsCustom();
	throw std::runtime_error("No resource provider registered for key: " + std::string(name));

	return nullptr;
}

ComputePassBuilder RenderGraphBuilder::BuildComputePass(std::string const& name) {
    return ComputePassBuilder(_graph.get(), this, name);
}
RenderPassBuilder RenderGraphBuilder::BuildRenderPass(std::string const& name) {
	return RenderPassBuilder(_graph.get(), this, name);
}