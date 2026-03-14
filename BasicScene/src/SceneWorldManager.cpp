#include "BasicScene/SceneWorldManager.h"

#include <stdexcept>

namespace br::scene {

SceneWorldManager& SceneWorldManager::GetInstance() {
    static SceneWorldManager instance;
    return instance;
}

void SceneWorldManager::Initialize(const SetupWorldCallback& setupWorld) {
    if (m_isAlive && m_world) {
        return;
    }

    m_world = std::make_unique<flecs::world>();
    if (setupWorld) {
        setupWorld(*m_world);
    }

    m_isAlive = true;
}

void SceneWorldManager::Cleanup() {
    if (!m_world) {
        m_isAlive = false;
        return;
    }

    m_isAlive = false;
    m_world->release();
    m_world.reset();
}

flecs::world& SceneWorldManager::GetWorld() {
    if (!m_world) {
        throw std::runtime_error("SceneWorldManager::GetWorld() called before initialization");
    }

    return *m_world;
}

const flecs::world& SceneWorldManager::GetWorld() const {
    if (!m_world) {
        throw std::runtime_error("SceneWorldManager::GetWorld() called before initialization");
    }

    return *m_world;
}

} // namespace br::scene