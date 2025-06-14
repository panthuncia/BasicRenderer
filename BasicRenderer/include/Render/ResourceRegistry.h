#pragma once
#include "Resources/ResourceIdentifier.h"
#include <memory>
#include <map>
#include <vector>
#include <stdexcept>
#include <functional>

class Resource;
using OnResourceChangedFn = std::function<void(ResourceIdentifier, std::shared_ptr<Resource>)>;

class ResourceRegistry {
    struct Node {
        std::shared_ptr<Resource>  resource;      // if leaf
        std::map<std::string, Node> children;     // sub-namespaces
        std::vector<OnResourceChangedFn> listeners;
    };

    Node _root;

    // walk segments; create intermediate nodes if create==true
    Node* traverse(ResourceIdentifier const& id, bool create) {
        Node* cur = &_root;
        for (auto& seg : id.segments) {
            auto it = cur->children.find(seg);
            if (it == cur->children.end()) {
                if (!create) return nullptr;
                auto [nx, _] = cur->children.emplace(seg, Node{});
                it = nx;
            }
            cur = &it->second;
        }
        return cur;
    }

public:
    void Register(ResourceIdentifier const& id,
        std::shared_ptr<Resource> res)
    {
        auto* node = traverse(id, /*create=*/true);
        if (node->resource)
            throw std::runtime_error("Already registered: " + id.ToString());
        node->resource = std::move(res);
    }

    std::shared_ptr<Resource> Request(ResourceIdentifier const& id) const {
        auto* node = const_cast<ResourceRegistry*>(this)
            ->traverse(id, /*create=*/false);
        if (!node) return nullptr;//throw std::runtime_error("Unknown resource: " + id.ToString());
        if (!node->resource)
            throw std::runtime_error("'" + id.ToString() + "' is a namespace, not a leaf");
        return node->resource;
    }

    void Update(ResourceIdentifier const& id,
        std::shared_ptr<Resource> res)
    {
        auto* node = traverse(id, /*create=*/false);
        if (!node || !node->resource)
            throw std::runtime_error("Cannot update unregistered: " + id.ToString());
        node->resource = res;
        for (auto& cb : node->listeners)
            cb(id, res);
    }

    void AddListener(ResourceIdentifier const& id,
        OnResourceChangedFn cb)
    {
        auto* node = traverse(id, /*create=*/true);
        node->listeners.push_back(std::move(cb));
    }

    // list all immediate children names under 'id'
    std::vector<ResourceIdentifier> ListChildren(ResourceIdentifier const& id) const {
        const Node* node = const_cast<ResourceRegistry*>(this)
            ->traverse(id, /*create=*/false);
        if (!node) throw std::runtime_error("Namespace not found: " + id.ToString());
        std::vector<ResourceIdentifier> out;
        for (auto& [name, _] : node->children) {
            auto copy = id;
            copy.segments.push_back(name);
            out.push_back(std::move(copy));
        }
        return out;
    }
};

class ResourceRegistryView {
    ResourceRegistry& _global;
    std::vector<ResourceIdentifier>                              _allowedPrefixes;

public:
    // allowed may contain BOTH leaf-ids *and* namespace-prefix ids
    template<class Iterable>
    ResourceRegistryView(ResourceRegistry& R, Iterable const& allowed)
        : _global(R)
    {
        for (auto const& id : allowed)
            _allowedPrefixes.push_back(id);
    }

    ResourceRegistryView(ResourceRegistry& global)
        : _global(global) {
    }

    template<typename T = Resource>
    std::shared_ptr<T> Request(ResourceIdentifier const& id) const {
        // prefix check
        bool ok = false;
        for (auto const& prefix : _allowedPrefixes) {
            if (id == prefix || id.hasPrefix(prefix)) {
                ok = true; break;
            }
        }
        if (!ok)
            throw std::runtime_error(
                "Access denied to “" + id.ToString() + "” (not declared)");

        // delegate to global
        auto base = _global.Request(id);

        // cast
        if (auto   casted = std::dynamic_pointer_cast<T>(base))
            return casted;

        throw std::runtime_error(
            "Resource “" + id.ToString() + "” exists but is not the requested type");
    }

    // let the pass declare an entire namespace at once:
    bool DeclaredNamespace(ResourceIdentifier const& ns) const {
        for (auto const& p : _allowedPrefixes)
            if (p == ns) return true;
        return false;
    }
};