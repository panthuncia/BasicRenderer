#pragma once

#include <functional>

#include <flecs.h>
#include <rhi_allocator.h>

#include "resources/ResourceIdentifier.h"
#include "Managers/Singletons/ECSManager.h"

class TrackedAllocation {
public:
	TrackedAllocation() = default;

	TrackedAllocation(rhi::ma::AllocationPtr alloc, flecs::entity tok) noexcept
		: alloc_(std::move(alloc)), tok_(std::move(tok)) {
	}

	// Move-only
	TrackedAllocation(TrackedAllocation&&) noexcept = default;
	TrackedAllocation& operator=(TrackedAllocation&&) noexcept = default;

	// No copies
	TrackedAllocation(const TrackedAllocation&) = delete;
	TrackedAllocation& operator=(const TrackedAllocation&) = delete;

	rhi::ma::Allocation* Get() const noexcept { return alloc_.Get(); }
	rhi::ma::Allocation* operator->() const noexcept { return alloc_.Get(); }
	explicit operator bool() const noexcept { return static_cast<bool>(alloc_); }

	// Not sure if this is useful
	rhi::ma::AllocationPtr ReleaseAllocationAndDisarmTracking() noexcept {
		tok_.destruct();
		return std::move(alloc_);
	}

	// Should only run through DeletionManager
	~TrackedAllocation() { Reset(); }

private:
	void Reset() noexcept {
		alloc_.Reset(); // calls Allocation::ReleaseThis() via deleter
		if (ECSManager::GetInstance().IsAlive()) {
			tok_.destruct(); // flecs entity deletion
		}
	}
	rhi::ma::AllocationPtr alloc_;
	flecs::entity tok_;
};

struct EntityAttachBundle {
	std::vector<std::function<void(flecs::entity)>> ops;

	template<class T>
	EntityAttachBundle& Add() {
		ops.emplace_back([](flecs::entity e) { e.add<T>(); });
		return *this;
	}

	template<class T>
	EntityAttachBundle& Set(T value) {
		ops.emplace_back([v = std::move(value)](flecs::entity e) mutable { e.set<T>(v); });
		return *this;
	}

	// pairs/relationships
	template<class Rel>
	EntityAttachBundle& Pair(flecs::entity target) {
		ops.emplace_back([target](flecs::entity e) { e.add<Rel>(target); });
		return *this;
	}

	void ApplyTo(flecs::entity e) const {
		for (auto& op : ops) op(e);
	}
};

struct AllocationTrackDesc {
	// Optionally let caller provide an existing entity (rarely needed).
	flecs::entity existing = {};

	// Resource identifier
	std::optional<ResourceIdentifier> id;

	// Arbitrary attachments
	EntityAttachBundle attach;
};