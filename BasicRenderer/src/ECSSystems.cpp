#include "ECSSystems.h"

#include <spdlog/spdlog.h>

#include "Components.h"
#include "ObjectManager.h"
#include "MeshManager.h"
#include "LightManager.h"
#include "IndirectCommandBufferManager.h"
#include "CameraManager.h"
#include "MeshInstance.h"

void OnSceneActivated(flecs::world& world) {
	// System: When a scene root gets ActiveScene removed, process all child entities
	world.system<Components::ActiveScene>()
		.kind(flecs::OnAdd)
		.each([&](flecs::entity scene, const Components::ActiveScene&) {
		spdlog::info("[System] Scene activated: {}", scene.name().c_str());

		// renderable objects
		auto renderableObjectQuery = world.query_builder<Components::RenderableObject>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		renderableObjectQuery.each([&](flecs::entity child, Components::RenderableObject&) {
			spdlog::info("[System] Processing descendant entity with RenderableObject: {}", child.name().c_str());
			});

		// Mesh instances

		// Query for all entities that are descendants of the scene and have OpaqueMeshInstances.
		auto opaqueMeshQuery = world.query_builder<Components::OpaqueMeshInstances>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		opaqueMeshQuery.each([&](flecs::entity child, Components::OpaqueMeshInstances&) {
			spdlog::info("[System] Processing descendant entity with MeshInstances: {}", child.name().c_str());
			});
		// Query for all entities that are descendants of the scene and have AlphaTestMeshInstances.
		auto alphaTestMeshQuery = world.query_builder<Components::AlphaTestMeshInstances>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		alphaTestMeshQuery.each([&](flecs::entity child, Components::AlphaTestMeshInstances&) {
			spdlog::info("[System] Processing descendant entity with AlphaTestMeshInstances: {}", child.name().c_str());
			});
		// Query for all entities that are descendants of the scene and have BlendMeshInstances.
		auto blendMeshQuery = world.query_builder<Components::BlendMeshInstances>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		blendMeshQuery.each([&](flecs::entity child, Components::BlendMeshInstances&) {
			spdlog::info("[System] Processing descendant entity with BlendMeshInstances: {}", child.name().c_str());
			});

		// Lights
		auto lightQuery = world.query_builder<Components::Light>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		lightQuery.each([&](flecs::entity child, Components::Light&) {
			spdlog::info("[System] Processing descendant entity with Light: {}", child.name().c_str());
			});

		// Cameras
		auto cameraQuery = world.query_builder<Components::Camera>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		cameraQuery.each([&](flecs::entity child, const Components::Camera&) {
			spdlog::info("[System] Processing descendant entity with Camera: {}", child.name().c_str());
			});

			});
}

void OnSceneDeactivated(flecs::world& world) {
	// System: When a scene root gets ActiveScene removed, process all child entities
	world.system<Components::ActiveScene>()
		.kind(flecs::OnDelete)
		.each([&](flecs::entity scene, const Components::ActiveScene&) {
		spdlog::info("[System] Scene activated: {}", scene.name().c_str());

		// renderable objects
		auto renderableObjectQuery = world.query_builder<Components::RenderableObject>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		renderableObjectQuery.each([&](flecs::entity child, Components::RenderableObject&) {
			spdlog::info("[System] Processing descendant entity with RenderableObject: {}", child.name().c_str());
			});

		// Mesh instances

		// Query for all entities that are descendants of the scene and have OpaqueMeshInstances.
		auto opaqueMeshQuery = world.query_builder<Components::OpaqueMeshInstances>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		opaqueMeshQuery.each([&](flecs::entity child, Components::OpaqueMeshInstances&) {
			spdlog::info("[System] Processing descendant entity with MeshInstances: {}", child.name().c_str());
			});
		// Query for all entities that are descendants of the scene and have AlphaTestMeshInstances.
		auto alphaTestMeshQuery = world.query_builder<Components::AlphaTestMeshInstances>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		alphaTestMeshQuery.each([&](flecs::entity child, Components::AlphaTestMeshInstances&) {
			spdlog::info("[System] Processing descendant entity with AlphaTestMeshInstances: {}", child.name().c_str());
			});
		// Query for all entities that are descendants of the scene and have BlendMeshInstances.
		auto blendMeshQuery = world.query_builder<Components::BlendMeshInstances>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		blendMeshQuery.each([&](flecs::entity child, Components::BlendMeshInstances&) {
			spdlog::info("[System] Processing descendant entity with BlendMeshInstances: {}", child.name().c_str());
			});

		// Lights
		auto lightQuery = world.query_builder<Components::Light>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		lightQuery.each([&](flecs::entity child, Components::Light&) {
			spdlog::info("[System] Processing descendant entity with Light: {}", child.name().c_str());
			});

		// Cameras
		auto cameraQuery = world.query_builder<Components::Camera>()
			.with(flecs::ChildOf, scene)
			.cascade() // Only consider entities that inherit from the scene.
			.build();
		cameraQuery.each([&](flecs::entity child, const Components::Camera&) {
			spdlog::info("[System] Processing descendant entity with Camera: {}", child.name().c_str());
			});

			});
}

void RegisterAllSystems(flecs::world& world,  LightManager* lightManager, MeshManager* meshManager, ObjectManager* objectManager, IndirectCommandBufferManager* indirectCommandManager, CameraManager* cameraManager) {

	/**/
	// System: Process entities on OnDelete of RenderableObjects, but only if they are in an active scene.
	world.observer<Components::RenderableObject>()
		.event(flecs::OnDelete)
		.with<Components::ActiveScene>().cascade() // Only consider entities inheriting ActiveScene.
		.each([&](flecs::entity e, Components::RenderableObject&) {
		spdlog::info("[System] RenderableObject removed in active scene on entity: {}", e.name().c_str());
		objectManager->RemoveObject(e.get<Components::ObjectDrawInfo>());
		e.remove<Components::ObjectDrawInfo>();
		auto opaqueMeshInstances = e.get_mut<Components::OpaqueMeshInstances>();
		unsigned int drawCount = 0;
		unsigned int drawCountOpaque = 0;
		unsigned int drawCountAlphaTest = 0;
		unsigned int drawCountBlend = 0;
		if (opaqueMeshInstances) {
			for (auto& meshInstance : opaqueMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
				drawCount++;
				drawCountOpaque++;
			}
		}
		auto alphaTestMeshInstances = e.get_mut<Components::AlphaTestMeshInstances>();
		if (alphaTestMeshInstances) {
			for (auto& meshInstance : alphaTestMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
				drawCount++;
				drawCountAlphaTest++;
			}
		}
		auto blendMeshInstances = e.get_mut<Components::BlendMeshInstances>();
		if (blendMeshInstances) {
			for (auto& meshInstance : blendMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
				drawCount++;
				drawCountBlend++;
			}
		}
		e.remove<Components::OpaqueMeshInstances>();
		e.remove<Components::AlphaTestMeshInstances>();
		e.remove<Components::BlendMeshInstances>();
		auto drawStats = world.get_mut<Components::DrawStats>();
		if (drawStats) {
			drawStats->numDrawsInScene -= drawCount;
			drawStats->numOpaqueDraws -= drawCountOpaque;
			drawStats->numAlphaTestDraws -= drawCountAlphaTest;
			drawStats->numBlendDraws -= drawCountBlend;
		}
		spdlog::info("[System] DrawInfo removed from RenderableObject: {}", e.name().c_str());
			});
}