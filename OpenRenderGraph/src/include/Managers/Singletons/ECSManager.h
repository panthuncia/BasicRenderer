#pragma once

#include <flecs.h>

class ECSManager {
public:
	static ECSManager& GetInstance();

	flecs::world& GetWorld() {
		return m_world;
	}

	const flecs::world& GetWorld() const {
		return m_world;
	}

	bool IsAlive() const {
		return true;
	}

private:
	ECSManager() = default;
	flecs::world m_world;
};

inline ECSManager& ECSManager::GetInstance() {
	static ECSManager instance;
	return instance;
}
