#include "ECSSystems.h">

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
		.each([&](flecs::entity scene, Components::ActiveScene&) {
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
		cameraQuery.each([&](flecs::entity child, Components::Camera&) {
			spdlog::info("[System] Processing descendant entity with Camera: {}", child.name().c_str());
			});

			});
}

void OnSceneDeactivated(flecs::world& world) {
	// System: When a scene root gets ActiveScene removed, process all child entities
	world.system<Components::ActiveScene>()
		.kind(flecs::OnDelete)
		.each([&](flecs::entity scene, Components::ActiveScene&) {
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
		cameraQuery.each([&](flecs::entity child, Components::Camera&) {
			spdlog::info("[System] Processing descendant entity with Camera: {}", child.name().c_str());
			});

			});
}

void RegisterAllSystems(flecs::world& world,  LightManager* lightManager, MeshManager* meshManager, ObjectManager* objectManager, IndirectCommandBufferManager* indirectCommandManager, CameraManager* cameraManager) {
	// System: Process entities on OnAdd of RenderableObjects, but only if they are in an active scene.
	world.system<Components::RenderableObject>()
		.kind(flecs::OnAdd)
		.with<Components::ActiveScene>().cascade() // Only consider entities inheriting ActiveScene.
		.each([&](flecs::entity e, Components::RenderableObject&) {
		spdlog::info("[System] RenderableObject added in active scene on entity: {}", e.name().c_str());
		Components::RenderableObject* renderableObject = e.get_mut<Components::RenderableObject>();
		const Components::OpaqueMeshInstances* opaqueMeshInstances = e.get<Components::OpaqueMeshInstances>();
		const Components::AlphaTestMeshInstances* alphaTestMeshInstances = e.get<Components::AlphaTestMeshInstances>();
		const Components::BlendMeshInstances* blendMeshInstances = e.get<Components::BlendMeshInstances>();
		auto drawInfo = objectManager->AddObject(renderableObject->perObjectCB, opaqueMeshInstances, alphaTestMeshInstances, blendMeshInstances);
		e.add<ObjectDrawInfo>(drawInfo);

		auto globalMeshLibrary = world.get_mut<Components::GlobalMeshLibrary>();
		if (drawInfo.opaque.has_value()) {
			e.add<Components::OpaqueMeshInstances>(drawInfo.opaque.value());
			for (auto& meshInstance : opaqueMeshInstances->meshInstances) {
				if (!globalMeshLibrary->meshes.contains(meshInstance->GetMesh()->GetGlobalID())) {
					globalMeshLibrary->meshes[meshInstance->GetMesh()->GetGlobalID()] = meshInstance->GetMesh();
					meshManager->AddMesh(meshInstance->GetMesh(), MaterialBuckets::Opaque);
				}
			}
		}
		if (drawInfo.alphaTest.has_value()) {
			e.add<Components::AlphaTestMeshInstances>(drawInfo.alphaTest.value());
			for (auto& meshInstance : alphaTestMeshInstances->meshInstances) {
				if (!globalMeshLibrary->meshes.contains(meshInstance->GetMesh()->GetGlobalID())) {
					globalMeshLibrary->meshes[meshInstance->GetMesh()->GetGlobalID()] = meshInstance->GetMesh();
					meshManager->AddMesh(meshInstance->GetMesh(), MaterialBuckets::AlphaTest);
				}
			}
		}
		if (drawInfo.blend.has_value()) {
			e.add<Components::BlendMeshInstances>(drawInfo.blend.value());
			for (auto& meshInstance : blendMeshInstances->meshInstances) {
				if (!globalMeshLibrary->meshes.contains(meshInstance->GetMesh()->GetGlobalID())) {
					globalMeshLibrary->meshes[meshInstance->GetMesh()->GetGlobalID()] = meshInstance->GetMesh();
					meshManager->AddMesh(meshInstance->GetMesh(), MaterialBuckets::Blend);
				}
			}
		}
		spdlog::info("[System 1] DrawInfo added to RenderableObject: {}", e.name().c_str());
			});

	// System: Process entities on OnDelete of RenderableObjects, but only if they are in an active scene.
	world.system<Components::RenderableObject>()
		.kind(flecs::OnDelete)
		.with<Components::ActiveScene>().cascade() // Only consider entities inheriting ActiveScene.
		.each([&](flecs::entity e, Components::RenderableObject&) {
		spdlog::info("[System] RenderableObject removed in active scene on entity: {}", e.name().c_str());
		objectManager->RemoveObject(e.get<ObjectDrawInfo>());
		e.remove<ObjectDrawInfo>();
		auto opaqueMeshInstances = e.get_mut<Components::OpaqueMeshInstances>();
		if (opaqueMeshInstances) {
			for (auto& meshInstance : opaqueMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
			}
		}
		auto alphaTestMeshInstances = e.get_mut<Components::AlphaTestMeshInstances>();
		if (alphaTestMeshInstances) {
			for (auto& meshInstance : alphaTestMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
			}
		}
		auto blendMeshInstances = e.get_mut<Components::BlendMeshInstances>();
		if (blendMeshInstances) {
			for (auto& meshInstance : blendMeshInstances->meshInstances) {
				meshManager->RemoveMeshInstance(meshInstance.get());
			}
		}
		e.remove<Components::OpaqueMeshInstances>();
		e.remove<Components::AlphaTestMeshInstances>();
		e.remove<Components::BlendMeshInstances>();
		spdlog::info("[System] DrawInfo removed from RenderableObject: {}", e.name().c_str());
			});
	
	// System: Process entities on OnAdd of MeshInstances, but only if they are in an active scene.
    world.system<Components::OpaqueMeshInstances>()
        .kind(flecs::OnAdd)
        .with<Components::ActiveScene>().cascade() // Only consider entities inheriting ActiveScene.
        .each([&](flecs::entity e, Components::OpaqueMeshInstances&) {
            spdlog::info("[System] MeshInstances added in active scene on entity: {}", e.name().c_str());
			auto meshInstances = e.get_mut<Components::OpaqueMeshInstances>();
			for (auto& meshInstance : meshInstances->meshInstances) {
				meshManager->AddMeshInstance(meshInstance.get());
			}
            });

	world.system<Components::AlphaTestMeshInstances>()
		.kind(flecs::OnAdd)
		.with<Components::ActiveScene>().cascade() // Only consider entities inheriting ActiveScene.
		.each([&](flecs::entity e, Components::AlphaTestMeshInstances&) {
		spdlog::info("[System] AlphaTestMeshInstances added in active scene on entity: {}", e.name().c_str());
		auto meshInstances = e.get_mut<Components::AlphaTestMeshInstances>();
		for (auto& meshInstance : meshInstances->meshInstances) {
			meshManager->AddMeshInstance(meshInstance.get());
		}
			});

	world.system<Components::BlendMeshInstances>()
		.kind(flecs::OnAdd)
		.with<Components::ActiveScene>().cascade() // Only consider entities inheriting ActiveScene.
		.each([&](flecs::entity e, Components::BlendMeshInstances&) {
		spdlog::info("[System] BlendMeshInstances added in active scene on entity: {}", e.name().c_str());
		auto meshInstances = e.get_mut<Components::BlendMeshInstances>();
		for (auto& meshInstance : meshInstances->meshInstances) {
			meshManager->AddMeshInstance(meshInstance.get());
		}
			});

}