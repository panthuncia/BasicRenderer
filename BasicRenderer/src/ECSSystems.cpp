#include "ECSSystems.h">

#include <spdlog/spdlog.h>

#include "Components.h"

void RegisterAllSystems(flecs::world& world) {
    // System 1: Process entities on OnAdd of MeshInstances, but only if they are in an active scene.
    world.system<Components::OpaqueMeshInstances>()
        .kind(flecs::OnAdd)
        .with<Components::ActiveScene>().cascade() // Only consider entities inheriting ActiveScene.
        .each([](flecs::entity e, Components::OpaqueMeshInstances&) {
            spdlog::info("[System 1] MeshInstances added in active scene on entity: {}", e.name().c_str());
            });

    // System 2: When a scene root gets ActiveScene, process all child entities
    world.system<Components::ActiveScene>()
        .kind(flecs::OnAdd)
        .each([&](flecs::entity scene, Components::ActiveScene&) {
		spdlog::info("[System 2] Scene activated: {}", scene.name().c_str());

        // Mesh instances

        // Query for all entities that are descendants of the scene and have OpaqueMeshInstances.
		auto opaqueMeshQuery = world.query_builder<Components::OpaqueMeshInstances>()
            .with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
            .build();
        opaqueMeshQuery.each([&](flecs::entity child, Components::OpaqueMeshInstances&) {
			spdlog::info("[System 2] Processing descendant entity with MeshInstances: {}", child.name().c_str());
            });
		// Query for all entities that are descendants of the scene and have AlphaTestMeshInstances.
		auto alphaTestMeshQuery = world.query_builder<Components::AlphaTestMeshInstances>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
        alphaTestMeshQuery.each([&](flecs::entity child, Components::AlphaTestMeshInstances&) {
            spdlog::info("[System 2] Processing descendant entity with AlphaTestMeshInstances: {}", child.name().c_str());
            });
		// Query for all entities that are descendants of the scene and have BlendMeshInstances.
		auto blendMeshQuery = world.query_builder<Components::BlendMeshInstances>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
        blendMeshQuery.each([&](flecs::entity child, Components::BlendMeshInstances&) {
            spdlog::info("[System 2] Processing descendant entity with BlendMeshInstances: {}", child.name().c_str());
            });

		// Lights
		auto lightQuery = world.query_builder<Components::Light>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
        lightQuery.each([&](flecs::entity child, Components::Light&) {
            spdlog::info("[System 2] Processing descendant entity with Light: {}", child.name().c_str());
            });

		// Cameras
		auto cameraQuery = world.query_builder<Components::Camera>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
        cameraQuery.each([&](flecs::entity child, Components::Camera&) {
            spdlog::info("[System 2] Processing descendant entity with Camera: {}", child.name().c_str());
            });

            });
}