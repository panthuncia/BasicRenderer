#pragma once

#include <vector>
#include <memory>

#include "Resources/ResourceStateTracker.h"

class IResourceResolver {
	public:
	virtual ~IResourceResolver() = default;
	virtual std::vector<std::shared_ptr<Resource>> Resolve() const = 0;

    template<typename T>
    std::vector<std::shared_ptr<T>> ResolveAs(bool require_all_casts = true) const {
        static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

        auto base = Resolve();
        std::vector<std::shared_ptr<T>> out;
        out.reserve(base.size());

        for (auto& p : base) {
            if (auto d = std::dynamic_pointer_cast<T>(p)) {
                out.push_back(std::move(d));
            }
            else if (require_all_casts) {
                assert(false && "Resource could not be cast to requested type");
            }
        }
        return out;
    }

    virtual std::unique_ptr<IResourceResolver> Clone() const = 0;
};

// Helper to avoid rewriting Clone in every derived type
template<class Derived>
struct ClonableResolver : IResourceResolver {
    std::unique_ptr<IResourceResolver> Clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
};

struct ResourceResolverAndRange {
    ResourceResolverAndRange(const IResourceResolver& resolver) {
        range = {}; // Full range
        // Copy resolver into unique_ptr
        pResolver = resolver.Clone();
    }
    ResourceResolverAndRange(const ResourceIdentifier& resource, const RangeSpec& range) : range(range) {}
    std::unique_ptr<IResourceResolver> pResolver;
    RangeSpec range;
};