#pragma once
#include "Resources/ResourceIdentifier.h"
#include <memory>
#include <map>
#include <vector>
#include <stdexcept>
#include <functional>

#include "Interfaces/IResourceResolver.h"

class Resource;
using OnResourceChangedFn = std::function<void(ResourceIdentifier, std::shared_ptr<Resource>)>;

class ResourceRegistry {

    struct ResourceKey {
        uint32_t idx = 0;
    };

    struct Slot {
        std::shared_ptr<Resource> resource;
        uint32_t generation = 1;
        ResourceIdentifier id; // for debug / access checks / reverse mapping
        bool alive = false;
    };

    std::vector<Slot> slots;
    std::vector<uint32_t> freeList;

    // Interning map: ResourceIdentifier -> ResourceKey
    std::unordered_map<ResourceIdentifier, ResourceKey, ResourceIdentifier::Hasher> intern;

public:

    struct ResourceHandle {
        ResourceKey key{};
        uint32_t generation = 0;   // for stale detection
        uint64_t epoch = 0;
    };

    ResourceKey InternKey(ResourceIdentifier const& id) {
        if (auto it = intern.find(id); it != intern.end()) return it->second;

        uint32_t idx;
        if (!freeList.empty()) { idx = freeList.back(); freeList.pop_back(); }
        else { idx = (uint32_t)slots.size(); slots.emplace_back(); }

        slots[idx].id = id;
        slots[idx].alive = true;
        ResourceKey key{ idx };
        intern.emplace(id, key);
        return key;
    }

    ResourceHandle RegisterOrUpdate(ResourceIdentifier const& id, std::shared_ptr<Resource> res) {
        ResourceKey key = InternKey(id);
        Slot& s = slots[key.idx];

        s.resource = res;
        s.generation++; // bump on replacement
        s.alive = true;

        return ResourceHandle{ key, s.generation };
    }

    ResourceHandle MakeHandle(ResourceIdentifier const& id) const {
        auto it = intern.find(id);
        if (it == intern.end()) return {}; // generation==0 means invalid

        const ResourceKey key = it->second;
        if (key.idx >= slots.size()) return {};

        const Slot& s = slots[key.idx];
        if (!s.alive || !s.resource) return {};

        ResourceHandle h;
        h.key = key;
        h.generation = s.generation;
        return h;
    }

    Resource* Resolve(const ResourceHandle h) {
        if (h.key.idx >= slots.size()) return nullptr;
        Slot& s = slots[h.key.idx];
        if (!s.alive || !s.resource) return nullptr;
        if (s.generation != h.generation) return nullptr; // stale handle
        return s.resource.get();
    }

    Resource const* Resolve(ResourceHandle h) const {
        return const_cast<ResourceRegistry*>(this)->Resolve(h);
    }

    // allow "floating" handles that follow replacements
    Resource* Resolve(ResourceKey k) {
        if (k.idx >= slots.size()) return nullptr;
        Slot& s = slots[k.idx];
        return (s.alive ? s.resource.get() : nullptr);
    }

    bool IsValid(ResourceHandle h) const noexcept {
        if (h.key.idx >= slots.size()) return false;
        const Slot& s = slots[h.key.idx];
        return s.alive && s.resource && s.generation == h.generation;
    }

    // Unchecked: no declared-prefix enforcement. For RenderGraph/internal use.
    std::shared_ptr<Resource> RequestShared(ResourceIdentifier const& id) const {
		auto it = intern.find(id);
        if (it == intern.end()) {
            return nullptr;
        }
		const ResourceKey key = it->second;
        if (key.idx >= slots.size()) {
            return nullptr;
        }
        const Slot& s = slots[key.idx];
        if (!s.alive || !s.resource) {
            return nullptr;
        }
		return s.resource;
    }

    template<class T>
    std::shared_ptr<T> RequestSharedAs(ResourceIdentifier const& id) const {
        auto base = RequestShared(id);
        return std::dynamic_pointer_cast<T>(base);
    }
};

class ResourceRegistryView {
    ResourceRegistry& _global;
    std::vector<ResourceIdentifier>  _allowedPrefixes;
    uint64_t epoch = 0; // guard
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

	// Move constructor
    ResourceRegistryView(ResourceRegistryView&& other) noexcept
        : _global(other._global), _allowedPrefixes(std::move(other._allowedPrefixes)) {
	}

    template<class T>
    T* Resolve(const ResourceRegistry::ResourceHandle h) const {
        if (h.epoch != epoch) {
            return nullptr;
        }
        Resource* r = _global.Resolve(h);
        auto casted = dynamic_cast<T*>(r);
        if (!casted) {
            throw std::runtime_error("Resource handle type mismatch");
        }
        return casted;
    }

    ResourceRegistry::ResourceHandle RequestHandle(ResourceIdentifier const& id) const {
        // prefix check (same as Request<T>)
        bool ok = false;
        for (auto const& prefix : _allowedPrefixes) {
            if (id == prefix || id.hasPrefix(prefix)) { ok = true; break; }
        }
        if (!ok) {
            throw std::runtime_error(
                "Access denied to \"" + id.ToString() + "\" (not declared)");
        }

        // mint handle from registry (key+generation), then stamp view epoch
        auto h = _global.MakeHandle(id);
        if (h.generation == 0) {
            throw std::runtime_error("Unknown resource: \"" + id.ToString() + "\"");
        }
        if (h.generation == 0) {
            // Shouldn't happen if base != nullptr, but keeps behavior robust.
            throw std::runtime_error("Failed to mint handle for: \"" + id.ToString() + "\"");
        }

        h.epoch = epoch;
        return h;
    }
    
    template<class T>
    T* RequestPtr(ResourceIdentifier const& id) const {
        auto h = RequestHandle(id);
        if (!IsValid(h)) return nullptr;
        return Resolve<T>(h);
    }

    bool IsValid(ResourceRegistry::ResourceHandle h) const noexcept {
        if (h.generation == 0) return false;
        if (h.epoch != epoch)  return false;

        // Delegate to registry for slot checks
        return _global.IsValid(h);
    }

    // let the pass declare an entire namespace at once:
    bool DeclaredNamespace(ResourceIdentifier const& ns) const {
        for (auto const& p : _allowedPrefixes)
            if (p == ns) return true;
        return false;
    }
};