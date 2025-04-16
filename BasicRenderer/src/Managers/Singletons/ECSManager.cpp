#include "Managers/Singletons/ECSManager.h"

#include <thread>

#include "Scene/Components.h"

void ECSManager::Initialize() {
	world.component<Components::ActiveScene>().add(flecs::Exclusive);

	flecs::entity game = world.pipeline()
		.with(flecs::System)
		.build();
	world.set<Components::GameScene>({ game });

	world.import<flecs::stats>();

	// Creates REST server on default port (27750)
	world.set<flecs::Rest>({});

	world.set_threads(8);
}

