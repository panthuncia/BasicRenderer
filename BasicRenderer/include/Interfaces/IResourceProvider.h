#pragma once

#include <memory>

#include "Resources/ResourceIdentifier.h"

class Resource;
class IResourceProvider {
public:
    virtual ~IResourceProvider() = default;
    virtual std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) = 0;
    virtual std::vector<ResourceIdentifier> GetSupportedKeys() = 0;
};