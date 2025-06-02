#pragma once

#include <memory>

#include "Interfaces/IResourceProvider.h"
#include "PassBuilders.h"

class RenderGraph;

class RenderGraphBuilder {
public:
    RenderGraphBuilder() {
        _graph = std::make_shared<RenderGraph>();
    }

    void RegisterProvider(IResourceProvider* prov) {
		auto keys = prov->GetSupportedKeys();
		for (const auto& key : keys) {
			if (_providerMap.find(key) != _providerMap.end()) {
				std::string_view name = key.IsBuiltin() ? BuiltinResourceToString(key.AsBuiltin()) : key.AsCustom();
				throw std::runtime_error("Resource provider already registered for key: " + std::string(name));
			}
			_providerMap[key] = prov;
		}
        _providers.push_back(prov);
    }

	void RegisterResource(ResourceIdentifier id, std::shared_ptr<Resource> resource,
		IResourceProvider* provider = nullptr) {
		if (_registry.find(id) != _registry.end()) {
			throw std::runtime_error("Resource already registered: " + id.ToString());
		}
		_registry[id] = resource;
		if (provider) {
			_providerMap[id] = provider;
		}
	}

    std::shared_ptr<Resource> RequestResource(ResourceIdentifier const& rid);

    ComputePassBuilder BuildComputePass(std::string const& name);
    RenderPassBuilder BuildRenderPass(std::string const& name);

	std::shared_ptr<RenderPass> GetRenderPassByName(const std::string& name) {
		return _graph->GetRenderPassByName(name);
	}

	std::shared_ptr<RenderGraph> Build() {
		if (m_isBuilt) {
			throw std::runtime_error("RenderGraph is already built");
		}
		_graph->Setup();
		_graph->Compile();
		m_isBuilt = true;
		return _graph;
	}


private:
	bool m_isBuilt = false;
    std::shared_ptr<RenderGraph> _graph;
    std::vector<IResourceProvider*> _providers;

    std::unordered_map<ResourceIdentifier, 
        std::shared_ptr<Resource>, 
        ResourceIdentifier::Hasher> 
        _registry;

	std::unordered_map<ResourceIdentifier, IResourceProvider*, ResourceIdentifier::Hasher> _providerMap;
};