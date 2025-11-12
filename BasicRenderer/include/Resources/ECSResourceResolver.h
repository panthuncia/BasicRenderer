#pragma once

#include <flecs.h>

#include "Interfaces/IResourceResolver.h"
#include "Resources/components.h"

class ECSResourceResolver : public IResourceResolver {
public:
	ECSResourceResolver() = default;
	ECSResourceResolver(flecs::query<> query)
		: m_query(query) {
	}

	virtual std::vector<std::shared_ptr<Resource>> Resolve() const  {
		std::vector<std::shared_ptr<Resource>> resources;
		m_query.each([&](flecs::entity e) {
#if BUILD_TYPE == BUILD_TYPE_DEBUG
			assert(e.has<Components::Resource>() && "Entity does not have Resource component");
#endif
			auto& res = e.get<Components::Resource>();
			resources.push_back(res.resource);
			});
	}

protected:
	flecs::query<> m_query;
};