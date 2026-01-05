#pragma once

#include <vector>
#include <memory>

#include "Interfaces/IResourceResolver.h"
#include "Resources/ResourceGroup.h"

// A resolver that captures any flecs::query<...> by value
class ResourceGroupResolver : public ClonableResolver<ResourceGroupResolver> {
public:
    ResourceGroupResolver() = default;

    explicit ResourceGroupResolver(const std::shared_ptr<ResourceGroup>& resourceGroup)
        : m_resourceGroup(resourceGroup) {
    }

    std::vector<std::shared_ptr<Resource>> Resolve() const override {
		return m_resourceGroup->GetChildren();
    }

private:
    std::shared_ptr<ResourceGroup> m_resourceGroup;
};