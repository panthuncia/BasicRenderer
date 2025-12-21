#pragma once

#include <functional>
#include <variant>

#include <flecs.h>
#include <rhi_allocator.h>

#include "resources/ResourceIdentifier.h"
#include "Managers/Singletons/ECSManager.h"

struct EntityComponentBundle {
    std::vector<std::function<void(flecs::entity)>> ops;

    template<class T>
    EntityComponentBundle& Add() {
        ops.emplace_back([](flecs::entity e) { e.add<T>(); });
        return *this;
    }

    template<class T>
    EntityComponentBundle& Set(T value) {
        ops.emplace_back([v = std::move(value)](flecs::entity e) mutable { e.set<T>(v); });
        return *this;
    }

    // pairs/relationships
    template<class Rel>
    EntityComponentBundle& Pair(flecs::entity target) {
        ops.emplace_back([target](flecs::entity e) { e.add<Rel>(target); });
        return *this;
    }

    void ApplyTo(flecs::entity e) const {
        for (auto& op : ops) op(e);
    }
};

struct TrackedEntityToken {
    flecs::world* world = nullptr;
    flecs::entity_t id = 0;

    TrackedEntityToken() = default;
    TrackedEntityToken(flecs::world& w, flecs::entity_t e) noexcept : world(&w), id(e) {}

    TrackedEntityToken(TrackedEntityToken&& o) noexcept
        : world(std::exchange(o.world, nullptr))
        , id(std::exchange(o.id, 0)) {
    }

    TrackedEntityToken& operator=(TrackedEntityToken&& o) noexcept {
        if (this != &o) {
            Reset();
            world = std::exchange(o.world, nullptr);
            id = std::exchange(o.id, 0);
        }
        return *this;
    }

    TrackedEntityToken(const TrackedEntityToken&) = delete;
    TrackedEntityToken& operator=(const TrackedEntityToken&) = delete;

    ~TrackedEntityToken() { Reset(); }

    void ApplyAttachBundle(const EntityComponentBundle& bundle) const noexcept {
        if (world && id) {
			auto e = flecs::entity{ *world, id };
			bundle.ApplyTo(e);
        }
    }

    void Disarm() noexcept { world = nullptr; id = 0; }

    void Reset() noexcept {
        if (world && id && ECSManager::GetInstance().IsAlive()) {
            flecs::entity e{ *world, id };
            if (e.is_alive()) e.destruct();
        }
        Disarm();
    }
};

class TrackedHandle {
public:
    TrackedHandle() = default;

    static TrackedHandle FromAllocation(rhi::ma::AllocationPtr a, TrackedEntityToken t) noexcept {
        TrackedHandle h;
        h.h_ = std::move(a);
        h.tok_ = std::move(t);
        return h;
    }

    static TrackedHandle FromResource(rhi::ResourcePtr r, TrackedEntityToken t) noexcept {
        TrackedHandle h;
        h.h_ = std::move(r);
        h.tok_ = std::move(t);
        return h;
    }

    TrackedHandle(TrackedHandle&&) noexcept = default;
    TrackedHandle& operator=(TrackedHandle&&) noexcept = default;

    TrackedHandle(const TrackedHandle&) = delete;
    TrackedHandle& operator=(const TrackedHandle&) = delete;

    ~TrackedHandle() { Reset(); }

    void ApplyComponentBundle(const EntityComponentBundle& bundle) noexcept {
        tok_.ApplyAttachBundle(bundle);
    }

    explicit operator bool() const noexcept {
        return std::visit([](auto const& v) -> bool {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::monostate>) return false;
            else return static_cast<bool>(v);
            }, h_);
    }

    rhi::Resource& GetResource() noexcept {
        return std::visit([](auto& v) -> rhi::Resource& {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, rhi::ma::AllocationPtr>) return v.Get()->GetResource();
            else if constexpr (std::is_same_v<V, rhi::ResourcePtr>) return v.Get();
            else std::terminate(); // or assert
            }, h_);
    }

    // Called by DeletionManager when it's actually time to free.
    void Reset() noexcept {
        std::visit([](auto& v) {
            using V = std::decay_t<decltype(v)>;
            if constexpr (!std::is_same_v<V, std::monostate>) v.Reset();
            }, h_);
        h_ = std::monostate{};
        tok_.Reset(); // enqueues entity deletion (main thread flush later)
    }

    // If you want to hand out the underlying pointer and keep the entity alive:
    rhi::ma::AllocationPtr ReleaseAllocationDisarm() noexcept {
        tok_.Disarm();
        if (auto* p = std::get_if<rhi::ma::AllocationPtr>(&h_)) return std::move(*p);
        return {};
    }

    rhi::ResourcePtr ReleaseResourceDisarm() noexcept {
        tok_.Disarm();
        if (auto* p = std::get_if<rhi::ResourcePtr>(&h_)) return std::move(*p);
        return {};
    }

private:
    std::variant<std::monostate, rhi::ma::AllocationPtr, rhi::ResourcePtr> h_;
    TrackedEntityToken tok_;
};

struct AllocationTrackDesc {
    AllocationTrackDesc(const int globalResourceID)
    {
	    this->globalResourceID = globalResourceID;
    }

    int globalResourceID;
	// Optionally let caller provide an existing entity (rarely needed).
	flecs::entity existing = {};

	// Resource identifier
	std::optional<ResourceIdentifier> id;

	// Arbitrary attachments
    EntityComponentBundle attach;
};