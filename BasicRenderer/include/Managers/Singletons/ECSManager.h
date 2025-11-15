#pragma once
#include <flecs.h>
#include <unordered_map>

class ECSManager {
public:
    static ECSManager& GetInstance();
	void Initialize();
	flecs::world& GetWorld() { return world; }

private:
    flecs::world world;
    ECSManager() = default;
};

inline ECSManager& ECSManager::GetInstance() {
    static ECSManager instance;
    return instance;
}