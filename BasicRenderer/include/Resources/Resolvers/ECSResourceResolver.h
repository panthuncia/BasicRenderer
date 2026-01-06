#pragma once

#include <flecs.h>
#include <functional>
#include <vector>
#include <memory>

#include "Interfaces/IResourceResolver.h"
#include "Resources/components.h"

// A resolver that captures any flecs::query<...> by value
class ECSResourceResolver : public ClonableResolver<ECSResourceResolver> {
public:
    ECSResourceResolver() = default;

    // Capture any flecs query (e.g. flecs::query<flecs::entity>, flecs::query<Cs...>)
    template<typename QueryT>
    explicit ECSResourceResolver(QueryT query) {
        // Move the query into a closure to keep it alive for the resolver lifetime.
        m_enumerator = [q = std::move(query)](std::vector<std::shared_ptr<Resource>>& out) {
            q.each([&](flecs::entity e) {
#if BUILD_TYPE == BUILD_TYPE_DEBUG
                assert(e.has<Components::Resource>() && "Entity does not have Resource component");
#endif
                auto& res = e.get<Components::Resource>();
                auto shared = res.resource.lock();
                out.push_back(shared);
            });
        };
    }

    std::vector<std::shared_ptr<Resource>> Resolve() const override {
        std::vector<std::shared_ptr<Resource>> resources;
        if (m_enumerator) {
            m_enumerator(resources);
        }
        return resources;
    }

private:
    std::function<void(std::vector<std::shared_ptr<Resource>>&)> m_enumerator;
};