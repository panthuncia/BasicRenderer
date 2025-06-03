#include "Render/RenderGraphBuilder.h"

#include "Render/RenderGraph.h"
#include "Resources/GloballyIndexedResource.h"

std::shared_ptr<Resource> RenderGraphBuilder::RequestResource(ResourceIdentifier const& rid, bool allowFailure) {
    // If it's already in our registry, return it
    auto it = _registry.find(rid);
    if (it != _registry.end()) 
        return it->second;

	auto providerIt = _providerMap.find(rid);
	if (providerIt != _providerMap.end()) {
		// If we have a provider for this key, use it to provide the resource
		auto provider = providerIt->second;
		if (provider) {
			auto resource = provider->ProvideResource(rid);
			if (resource) {
				// Register the resource in our registry
				_registry[rid] = resource;
				_graph->AddResource(resource);
				return resource;
			}
			else {
				throw std::runtime_error("Provider returned null for key: " + rid.ToString());
			}
		}
	}

	// No provider registered for this key
	if (allowFailure) {
		// If we are allowed to fail, return nullptr
		return nullptr;
	}
	std::string_view name = rid.IsBuiltin() ? BuiltinResourceToString(rid.AsBuiltin()) : rid.AsCustom();
	throw std::runtime_error("No resource provider registered for key: " + std::string(name));
}

std::shared_ptr<GloballyIndexedResource> RenderGraphBuilder::RequestGloballyIndexedResource(ResourceIdentifier const& rid, bool allowFailure) {
	auto resource = RequestResource(rid, allowFailure);
	if (resource == nullptr) {
		if (allowFailure) {
			return nullptr; // If we are allowed to fail, return nullptr
		}
		throw std::runtime_error("Requested resource is null: " + rid.ToString());
	}
	if (auto indexedResource = std::dynamic_pointer_cast<GloballyIndexedResource>(resource)) {
		return indexedResource;
	}
	else {
		throw std::runtime_error("Requested resource is not a GloballyIndexedResource: " + rid.ToString());
	}
}


ComputePassBuilder RenderGraphBuilder::BuildComputePass(std::string const& name) {
    return ComputePassBuilder(_graph.get(), this, name);
}
RenderPassBuilder RenderGraphBuilder::BuildRenderPass(std::string const& name) {
	return RenderPassBuilder(_graph.get(), this, name);
}