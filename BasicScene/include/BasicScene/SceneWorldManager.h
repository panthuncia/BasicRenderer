#pragma once

#include <functional>
#include <memory>

#include <flecs.h>

namespace br::scene {

class SceneWorldManager {
public:
    using SetupWorldCallback = std::function<void(flecs::world&)>;

    static SceneWorldManager& GetInstance();

    void Initialize(const SetupWorldCallback& setupWorld = {});
    void Cleanup();

    flecs::world& GetWorld();
    const flecs::world& GetWorld() const;

    bool IsAlive() const {
        return m_isAlive;
    }

private:
    SceneWorldManager() = default;

    std::unique_ptr<flecs::world> m_world;
    bool m_isAlive = false;
};

} // namespace br::scene