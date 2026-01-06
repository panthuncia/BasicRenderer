#pragma once
#include "Resources/ResourceIdentifier.h"
#include <memory>
#include <map>
#include <vector>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include <optional>
#include <spdlog/spdlog.h>

#include "Resources/Resource.h"
#include "Resources/ResourceStateTracker.h"
#include "Interfaces/IResourceResolver.h"

class Resource;
using OnResourceChangedFn = std::function<void(ResourceIdentifier, std::shared_ptr<Resource>)>;

// TODO: Actually use Epoch?
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

    class RegistryHandle {
    public:
		RegistryHandle(ResourceKey k, 
            uint32_t gen, 
            uint64_t ep, 
            uint64_t globalIdx,
            SymbolicTracker* tracker,
            uint32_t numMips = 0, 
            uint32_t arraySz = 0)
            : key(k),
    	generation(gen), 
        tracker(tracker),
    	epoch(ep), 
    	globalResourceIndex(globalIdx),
    	numMipLevels(numMips), 
    	arraySize(arraySz) {}
		RegistryHandle() = default;
    	const ResourceKey& GetKey() const { return key; }
        uint32_t GetGeneration() const { return generation; }
        uint64_t GetEpoch() const { return epoch; }
        uint64_t GetGlobalResourceID() const { return globalResourceIndex; }
        uint32_t GetNumMipLevels() const { return numMipLevels; }
        uint32_t GetArraySize() const { return arraySize; }
		SymbolicTracker* GetStateTracker() const { return tracker; }
        // For ephemeral handles that bypass registry storage
        static RegistryHandle MakeEphemeral(Resource* raw) {
            RegistryHandle h;
            h.key = ResourceKey{ kEphemeralSlotIndex };
            h.generation = 0;
            h.epoch = 0;
            h.globalResourceIndex = raw->GetGlobalResourceID();
            h.tracker = raw->GetStateTracker();
            h.numMipLevels = raw->GetMipLevels();
            h.arraySize = raw->GetArraySize();
            h.ephemeralPtr = raw;
            return h;
        }
        Resource* GetEphemeralPtr() const { return ephemeralPtr; }
        bool IsEphemeral() const { return key.idx == kEphemeralSlotIndex && generation == 0; }
    private:
    	ResourceKey key{};
        uint32_t generation = 0;   // for stale detection
        SymbolicTracker* tracker;
        uint64_t epoch = 0;
        uint64_t globalResourceIndex; // For convenience

        uint32_t numMipLevels;
        uint32_t arraySize;
        Resource* ephemeralPtr = nullptr;  // Only set for ephemeral handles
    };

    void RegisterResolver(ResourceIdentifier const& id, std::shared_ptr<IResourceResolver> resolver) {
        m_resolvers[id] = std::move(resolver);
    }

    std::shared_ptr<IResourceResolver> GetResolver(ResourceIdentifier const& id) const {
        if (auto it = m_resolvers.find(id); it != m_resolvers.end()) {
            return it->second;
        }
        return nullptr;
    }

    bool HasResolver(ResourceIdentifier const& id) const {
        return m_resolvers.contains(id);
    }

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

    RegistryHandle MakeEphemeralHandle(Resource* res) const {
        if (!res) {
            return RegistryHandle({}, 0, 0, 0, nullptr, 0, 0);
        }

		// If the resource is already registered, return a normal handle
        // TODO: Is this fully valid? Or should we error? The user probably isn't doing this intentionally.
        if (auto existingHandle = GetHandleFor(res); existingHandle.has_value()) {
			spdlog::warn("Making ephemeral handle for already-registered resource '{}'. Returning normal handle instead.", res->GetName());
            return existingHandle.value();
		}

        // Use a sentinel index that won't collide with real slots
        // The handle is valid for dispatch purposes but won't resolve through the registry
        return RegistryHandle(
            ResourceKey{ kEphemeralSlotIndex },  // sentinel value
            0,  // generation 0
            m_epoch,
            res->GetGlobalResourceID(),
            res->GetStateTracker(),
            res->GetMipLevels(),
            res->GetArraySize()
        );
    }

    RegistryHandle RegisterOrUpdate(ResourceIdentifier const& id, std::shared_ptr<Resource> res) {
        ResourceKey key = InternKey(id);
        Slot& s = slots[key.idx];

        s.resource = res;
        s.generation++; // bump on replacement
        s.alive = true;

        RegistryHandle h(key,
            s.generation,
            m_epoch,
            res->GetGlobalResourceID(),
            res->GetStateTracker(),
            res->GetMipLevels(), 
            res->GetArraySize());

        resourceToHandle[s.resource.get()] = h;

        return h;
    }

    RegistryHandle RegisterAnonymous(std::shared_ptr<Resource> res) {
        uint32_t idx;
        if (!freeList.empty()) { idx = freeList.back(); freeList.pop_back(); }
        else { idx = (uint32_t)slots.size(); slots.emplace_back(); }

        Slot& s = slots[idx];

        // If slot previously held a resource, remove reverse mapping.
        if (s.resource) {
            resourceToHandle.erase(s.resource.get());
        }

        s.resource = std::move(res);
        s.generation++;
        s.alive = true;
        // s.id left default/empty (debug only)

        RegistryHandle h(
            ResourceKey{ idx },
            s.generation,
            m_epoch,
            s.resource->GetGlobalResourceID(),
            s.resource->GetStateTracker(),
            s.resource->GetMipLevels(),
            s.resource->GetArraySize()
        );

        resourceToHandle[s.resource.get()] = h;
        return h;
    }

    std::optional<RegistryHandle> GetHandleFor(Resource* res) const {
		if (res == nullptr) return std::nullopt;
        if (auto it = resourceToHandle.find(res); it != resourceToHandle.end()) {
            return it->second;
		}
		return std::nullopt;
    }

    std::optional<RegistryHandle> GetHandleFor(ResourceIdentifier const& id) const {
        auto it = intern.find(id);
        if (it == intern.end()) {
            return std::nullopt;
        }
        const ResourceKey key = it->second;
        if (key.idx >= slots.size()) {
            return std::nullopt;
        }
        const Slot& s = slots[key.idx];
        return GetHandleFor(s.resource.get());
    }

    RegistryHandle MakeHandle(ResourceIdentifier const& id) const {
        auto it = intern.find(id);
        if (it == intern.end()) return RegistryHandle({}, 0, 0, 0, 0, 0); // generation==0 means invalid

        const ResourceKey key = it->second;
        if (key.idx >= slots.size()) return RegistryHandle({}, 0, 0, 0, 0, 0);

        const Slot& s = slots[key.idx];
        if (!s.alive || !s.resource) return RegistryHandle({}, 0, 0, 0, 0, 0);

        RegistryHandle h(
            key,
            s.generation,
            m_epoch,
            s.resource->GetGlobalResourceID(),
            s.resource->GetStateTracker(),
                s.resource->GetMipLevels(), 
                s.resource->GetArraySize());

        return h;
    }

    Resource* Resolve(const RegistryHandle h) {
        if (h.IsEphemeral()) {
            return h.GetEphemeralPtr();
        }
        if (h.GetKey().idx >= slots.size()) {
            return nullptr;
        }
        Slot& s = slots[h.GetKey().idx];
        if (!s.alive || !s.resource) {
            return nullptr;
        }
        if (s.generation != h.GetGeneration()) {
            return nullptr; // stale handle
        }
        return s.resource.get();
    }

    Resource const* Resolve(RegistryHandle h) const {
        return const_cast<ResourceRegistry*>(this)->Resolve(h);
    }

    // allow "floating" handles that follow replacements
    Resource* Resolve(ResourceKey k) {
        if (k.idx >= slots.size()) return nullptr;
        Slot& s = slots[k.idx];
        return (s.alive ? s.resource.get() : nullptr);
    }

    bool IsValid(RegistryHandle h) const noexcept {
        if (h.GetKey().idx >= slots.size()) return false;
        const Slot& s = slots[h.GetKey().idx];
        return s.alive && s.resource && s.generation == h.GetGeneration();
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

private:
    uint64_t m_epoch = 0;
    std::unordered_map<Resource*, RegistryHandle> resourceToHandle;
	static constexpr uint32_t kEphemeralSlotIndex = UINT32_MAX;
    std::unordered_map<ResourceIdentifier, std::shared_ptr<IResourceResolver>, ResourceIdentifier::Hasher> m_resolvers;
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
    T* Resolve(const ResourceRegistry::RegistryHandle h) const {
        if (h.GetEpoch() != epoch) {
            return nullptr;
        }
        Resource* r = _global.Resolve(h);
        auto casted = dynamic_cast<T*>(r);
        if (!casted) {
            throw std::runtime_error("Resource handle type mismatch");
        }
        return casted;
    }

    ResourceRegistry::RegistryHandle RequestHandle(ResourceIdentifier const& id) const {
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
        if (h.GetGeneration() == 0) {
            throw std::runtime_error("Unknown resource: \"" + id.ToString() + "\"");
        }
        if (h.GetGeneration() == 0) {
            // Shouldn't happen if base != nullptr, but keeps behavior robust.
            throw std::runtime_error("Failed to mint handle for: \"" + id.ToString() + "\"");
        }

        //h.epoch = epoch;
        return h;
    }
    
    template<class T>
    T* RequestPtr(ResourceIdentifier const& id) const {
        auto h = RequestHandle(id);
        if (!IsValid(h)) return nullptr;
        return Resolve<T>(h);
    }

    std::shared_ptr<IResourceResolver> RequestResolver(ResourceIdentifier const& id) const {
        // Prefix check (same as resources)
        bool ok = false;
        for (auto const& prefix : _allowedPrefixes) {
            if (id == prefix || id.hasPrefix(prefix)) { ok = true; break; }
        }
        if (!ok) {
            throw std::runtime_error(
                "Access denied to resolver \"" + id.ToString() + "\" (not declared)");
        }

        auto resolver = _global.GetResolver(id);
        if (!resolver) {
            throw std::runtime_error("Unknown resolver: \"" + id.ToString() + "\"");
        }
        return resolver;
    }

    template<typename T>
    std::vector<std::shared_ptr<T>> ResolveAs(ResourceIdentifier const& id) const {
        auto resolver = RequestResolver(id);
        return resolver->ResolveAs<T>();
    }

    bool IsValid(ResourceRegistry::RegistryHandle h) const noexcept {
        if (h.GetGeneration() == 0) return false;
        if (h.GetEpoch() != epoch)  return false;

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