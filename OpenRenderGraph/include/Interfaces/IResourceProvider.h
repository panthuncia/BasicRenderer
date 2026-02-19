#pragma once

#include <memory>

#include "Resources/ResourceIdentifier.h"
#include "Interfaces/IResourceResolver.h"

class Resource;
class IResourceProvider {
public:
    virtual ~IResourceProvider() = default;
    virtual std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) = 0;
    virtual std::vector<ResourceIdentifier> GetSupportedKeys() = 0;
    virtual std::shared_ptr<IResourceResolver> ProvideResolver(ResourceIdentifier const& key) { return nullptr; }
    virtual std::vector<ResourceIdentifier> GetSupportedResolverKeys() { return {}; }
};