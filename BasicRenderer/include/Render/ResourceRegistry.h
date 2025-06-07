#pragma once
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stdexcept>

#include "Resources/ResourceIdentifier.h"

using OnResourceChangedFn = std::function<void(ResourceIdentifier, std::shared_ptr<Resource>)>;

class ResourceRegistry {
public:
	void Register(ResourceIdentifier id, std::shared_ptr<Resource> resource) {
		if (_map.find(id) != _map.end()) {
			throw std::runtime_error("Resource already registered: " + id.ToString());
		}
		_map[id] = resource;
	}
	std::shared_ptr<Resource> Request(ResourceIdentifier const& rid) {
		auto it = _map.find(rid);
		if (it != _map.end())
			return it->second;
		return nullptr;
	}
	void Update(ResourceIdentifier id, std::shared_ptr<Resource> r) {
		_map[id] = r;
		for (auto& cb : _listeners[id])
			cb(id, r);
	}
	void AddListener(ResourceIdentifier id, OnResourceChangedFn cb) {
		_listeners[id].push_back(cb);
	}
private:
	std::unordered_map<ResourceIdentifier,
		std::shared_ptr<Resource>,
		ResourceIdentifier::Hasher>
		_map;
	std::unordered_map<ResourceIdentifier, std::vector<OnResourceChangedFn>, ResourceIdentifier::Hasher> _listeners;
};

class ResourceRegistryView {
public:
	template<class Iterable>
	ResourceRegistryView(ResourceRegistry& global, Iterable const& allowed)
		: _global(global)
		, _allowed(allowed.begin(), allowed.end())
	{}

	ResourceRegistryView(ResourceRegistry& global)
		: _global(global){}

	template<typename T = Resource>
	std::shared_ptr<T> Request(ResourceIdentifier const& id) {
		if (!_allowed.count(id)) {
			throw std::runtime_error("Pass tried to fetch resource it didn’t declare: " + id.ToString());
		}
		return std::dynamic_pointer_cast<T>(_global.Request(id));
	}
private:
	const ResourceRegistry& _global;
	std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> _allowed;
};