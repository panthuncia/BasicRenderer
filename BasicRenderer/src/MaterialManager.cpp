#include "MaterialManager.h"

void MaterialManager::CacheMaterial(std::shared_ptr<Material> material) {
    if (m_materialCache.find(material->name) == m_materialCache.end()) {
        m_materialCache[material->name] = material;
    }
}

std::shared_ptr<Material> MaterialManager::GetMaterial(std::string name) {
    return m_materialCache[name];
}