#include "ECSManager.h"

#include "Components.h"

void ECSManager::Initialize() {
	world.component<Components::ActiveScene>().add(flecs::Exclusive);

	flecs::entity game = world.pipeline()
		.with(flecs::System)
		.build();
	world.set<Components::GameScene>({ game });
}

