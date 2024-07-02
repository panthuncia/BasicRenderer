#pragma once

#include <unordered_map>
#include <string>
#include <memory>

#include "Material.h"

class MaterialManager {
public:
    static MaterialManager& getInstance();

    std::shared_ptr<Material> GetMaterial(std::string name);
    void CacheMaterial(std::shared_ptr<Material> material);

private:
    MaterialManager() = default;
    std::unordered_map<std::string, std::shared_ptr<Material>> m_materialCache;
};

inline MaterialManager& MaterialManager::getInstance() {
    static MaterialManager instance;
    return instance;
}