#pragma once

#include "ResourceGroup.h"
#include "Texture.h"
#include ""

class ShadowMaps : public ResourceGroup {
public:
    ShadowMaps(const std::string& name)
        : ResourceGroup(name) {}

    void AddMap(std::shared_ptr<Texture> map) {
        AddResource(map);
    }
private:
};