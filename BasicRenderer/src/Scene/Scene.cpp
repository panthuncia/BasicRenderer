#include <BasicScene/Scene.h>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <execution>
#include <flecs.h>
#include <BasicScene/SceneWorldManager.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "Managers/Singletons/RendererECSManager.h"
#include <BasicScene/Components.h>
#include "Materials/Material.h"
#include "Managers/ObjectManager.h"
#include "Managers/MeshManager.h"
#include "Managers/LightManager.h"
#include "Managers/IndirectCommandBufferManager.h"
#include "Managers/SkeletonManager.h"
#include "Managers/MaterialManager.h"
#include "Mesh/MeshInstance.h"
#include "Animation/AnimationController.h"
#include "Utilities/MathUtils.h"
#include "Resources/Sampler.h"
#include "Resources/components.h"
#include "Resources/PixelBuffer.h"
#include "Render/DrawWorkload.h"

namespace {
	std::atomic<uint64_t> globalStableSceneId = 0;

	void EnsureSceneWorldInitialized() {
		auto& worldManager = br::scene::SceneWorldManager::GetInstance();
		if (worldManager.IsAlive()) {
			return;
		}

		worldManager.Initialize([](flecs::world& world) {
			world.component<Components::ActiveScene>().add(flecs::Exclusive);
			world.component<Components::GlobalMeshLibrary>().add(flecs::Exclusive);
			world.component<Components::DrawStats>("DrawStats").add(flecs::Exclusive);
			world.component<Components::ActiveScene>().add(flecs::OnInstantiate, flecs::Inherit);
			world.add<Components::GlobalMeshLibrary>();
			world.set<Components::DrawStats>({ 0, {} });

			flecs::entity game = world.pipeline()
				.with(flecs::System)
				.build();
			world.set<Components::GameScene>({ game });
			world.import<flecs::stats>();
			world.set<flecs::Rest>({});
			world.set_threads(8);

				// Mark entities dirty when local transforms change
			world.observer<Components::Position>()
				.event(flecs::OnSet)
				.each([](flecs::entity e, Components::Position&) {
					e.add<Components::TransformDirty>();
				});
			world.observer<Components::Rotation>()
				.event(flecs::OnSet)
				.each([](flecs::entity e, Components::Rotation&) {
					e.add<Components::TransformDirty>();
				});
			world.observer<Components::Scale>()
				.event(flecs::OnSet)
				.each([](flecs::entity e, Components::Scale&) {
					e.add<Components::TransformDirty>();
				});

			// Transform system: only recompute matrices for dirty entities
			world.system<const Components::Position, const Components::Rotation, const Components::Scale, const Components::Matrix*, Components::Matrix>()
				.with<Components::Active>()
				.with<Components::TransformDirty>()
				.term_at(3).parent().cascade()
				.cached().cache_kind(flecs::QueryCacheAll)
				.each([](flecs::entity e, const Components::Position& position, const Components::Rotation& rotation, const Components::Scale& scale, const Components::Matrix* matrix, Components::Matrix& output) {
					XMMATRIX matRotation = XMMatrixRotationQuaternion(rotation.rot);
					XMMATRIX matTranslation = XMMatrixTranslationFromVector(position.pos);
					XMMATRIX matScale = XMMatrixScalingFromVector(scale.scale);
					output.matrix = (matScale * matRotation * matTranslation);
					if (matrix != nullptr) {
						output.matrix = output.matrix * matrix->matrix;
					}
					e.remove<Components::TransformDirty>();
					e.add<Components::TransformUpdatedThisFrame>();
				});
		});
	}

	flecs::world& GetSceneWorld() {
		EnsureSceneWorldInitialized();
		return br::scene::SceneWorldManager::GetInstance().GetWorld();
	}

	Components::StableSceneID MakeStableSceneID() {
		return { globalStableSceneId.fetch_add(1, std::memory_order_relaxed) + 1 };
	}

	void AssignStableSceneID(flecs::entity entity) {
		entity.set<Components::StableSceneID>(MakeStableSceneID());
	}

	void ReassignStableSceneIDsRecursive(flecs::entity entity) {
		if (!entity.is_alive()) {
			return;
		}

		AssignStableSceneID(entity);
		entity.children([](flecs::entity child) {
			ReassignStableSceneIDsRecursive(child);
		});
	}

	void PropagateTransformDirtyToChildren(flecs::entity parent, std::unordered_set<uint64_t>& visited) {
		parent.children([&](flecs::entity child) {
			if (child.has<Components::Active>() && visited.insert(child.id()).second) {
				child.add<Components::TransformDirty>();
				PropagateTransformDirtyToChildren(child, visited);
			}
		});
	}

	template <typename Fn>
	void VisitSceneDescendants(flecs::entity root, Fn&& fn) {
		root.children([&](flecs::entity child) {
			if (child.has<Components::SceneRoot>()) {
				return;
			}

			fn(child);
			VisitSceneDescendants(child, fn);
		});
	}
}

std::atomic<uint64_t> Scene::globalSceneCount = 0;

Scene::Scene(){
	m_sceneID = globalSceneCount.fetch_add(1, std::memory_order_relaxed);

    getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
    getMaxShadowDistance = SettingsManager::GetInstance().getSettingGetter<float>("maxShadowDistance");
    setDirectionalLightCascadeSplits = SettingsManager::GetInstance().getSettingSetter<std::vector<float>>("directionalLightCascadeSplits");
	getMeshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader");

    //Initialize ECS scene
	auto& world = GetSceneWorld();
    ECSSceneRoot = world.entity().add<Components::SceneRoot>()
		.set<Components::Position>({0, 0, 0})
		.set<Components::Rotation>({0, 0, 0, 1})
		.set<Components::Scale>({1, 1, 1})
		.set<Components::Matrix>(DirectX::XMMatrixIdentity())
		.set<Components::Name>("Scene Root");
	AssignStableSceneID(ECSSceneRoot);
	ECSSceneRoot = ECSSceneRoot;
    world.set_pipeline(world.get<Components::GameScene>().pipeline);
}

flecs::entity Scene::CreateDirectionalLightECS(std::wstring name, XMFLOAT3 color, float intensity, XMFLOAT3 direction, bool shadowCasting){
	return CreateLightECS(name, Components::LightType::Directional, { 0, 0, 0 }, color, intensity, { 0, 0, 0 }, direction, 0, 0, shadowCasting);
}

flecs::entity Scene::CreatePointLightECS(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation, float linearAttenuation, float quadraticAttenuation, bool shadowCasting) {
	return CreateLightECS(name, Components::LightType::Point, position, color, intensity, { constantAttenuation, linearAttenuation, quadraticAttenuation }, {0, 0, 0}, 0, 0, shadowCasting);
}

flecs::entity Scene::CreateSpotLightECS(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle, float constantAttenuation, float linearAttenuation, float quadraticAttenuation, bool shadowCasting) {
	return CreateLightECS(name, Components::LightType::Spot, position, color, intensity, { constantAttenuation, linearAttenuation, quadraticAttenuation }, direction, innerConeAngle, outerConeAngle, shadowCasting);
}

flecs::entity Scene::CreateLightECS(std::wstring name, Components::LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 attenuation, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle, bool shadowCasting) {
	auto& world = GetSceneWorld();
	//float maxRange = 20.0f;
	XMVECTOR normalizedAttenuationVec = XMVector3Normalize(XMLoadFloat3(&attenuation));
	XMFLOAT3 normalizedAttenuation;
	XMStoreFloat3(&normalizedAttenuation, normalizedAttenuationVec);
	auto maxRange = CalculateLightRadius(intensity, normalizedAttenuation.x, normalizedAttenuation.y, normalizedAttenuation.z);

	LightInfo lightInfo;
	lightInfo.type = type;
	lightInfo.posWorldSpace = XMLoadFloat3(&position);
	DirectX::XMVECTOR lightColor = XMVector3Normalize(XMLoadFloat3(&color));
	// Set W to intensity
	lightColor = XMVectorSetW(lightColor, intensity);
	lightInfo.color = lightColor;
	float nearPlane = 0.01f;
	float farPlane = maxRange;
	lightInfo.attenuation = normalizedAttenuationVec;
	lightInfo.dirWorldSpace = XMLoadFloat3(&direction);
	lightInfo.innerConeAngle = cos(innerConeAngle);
	lightInfo.outerConeAngle = cos(outerConeAngle);
	lightInfo.shadowViewInfoIndex = -1;
	lightInfo.nearPlane = nearPlane;
	lightInfo.farPlane = farPlane;
	lightInfo.shadowCaster = shadowCasting;
	lightInfo.maxRange = maxRange;
	switch(type){
	case Components::LightType::Spot:
		lightInfo.boundingSphere = ComputeConeBoundingSphere(XMLoadFloat3(&position), XMLoadFloat3(&direction), maxRange, outerConeAngle);
		break;
	case Components::LightType::Point:
		lightInfo.boundingSphere = { { position.x, position.y, position.z, maxRange } };
	}

	flecs::entity entity = world.entity();
	entity.child_of(ECSSceneRoot)
		.set<Components::Light>({ type, color, normalizedAttenuation, maxRange, lightInfo })
		.set<Components::Position>(position)
		.set<Components::Scale>({ 1, 1, 1 })
		.set<Components::Matrix>(DirectX::XMMatrixIdentity())
		.set<Components::Name>(ws2s(name));
	AssignStableSceneID(entity);

	if (direction.x != 0 || direction.y != 0 || direction.z != 0) {
		entity.set<Components::Rotation>(QuaternionFromAxisAngle(direction));
	}
	else {
		entity.set<Components::Rotation>({ 0, 0, 0, 1 });
	}


	if (ECSSceneRoot.has<Components::ActiveScene>()) {
		ActivateLight(entity);
		entity.add<Components::Active>();
	}

	float aspect = 1.0f;

	switch (type) {
	case Components::LightType::Spot:
		entity.set<Components::ProjectionMatrix>({ XMMatrixPerspectiveFovRH(outerConeAngle * 2, aspect, nearPlane, farPlane) });
		break;
	case Components::LightType::Point:
		entity.set<Components::ProjectionMatrix>({ XMMatrixPerspectiveFovRH(XM_PI / 2, aspect, nearPlane, farPlane) });
		break;
	}

	std::vector<std::array<ClippingPlane, 6>> frustumPlanes;
	switch (type) {
	case Components::LightType::Directional:
		break; // Directional is special-cased, frustrums are in world space, calculated during cascade setup
	case Components::LightType::Spot: {
		frustumPlanes.push_back(GetFrustumPlanesPerspective(1.0f, outerConeAngle * 2, nearPlane, farPlane));
		entity.set<Components::FrustumPlanes>({ frustumPlanes });
		break;
	case Components::LightType::Point: {
		for (int i = 0; i < 6; i++) {
			frustumPlanes.push_back(GetFrustumPlanesPerspective(1.0f, XM_PI / 2, nearPlane, farPlane)); // TODO: All of these are the same.
		}
		entity.set<Components::FrustumPlanes>({ frustumPlanes });
		break;
	}
	}
	}
	
	return entity;
}

void Scene::ActivateRenderable(flecs::entity& entity) {
	auto& world = GetSceneWorld();

	auto meshInstances = entity.try_get<Components::MeshInstances>();
	//auto alphaTestMeshInstances = entity.try_get<Components::AlphaTestMeshInstances>();
	//auto blendMeshInstances = entity.try_get<Components::BlendMeshInstances>();

	auto& globalMeshLibrary = world.get_mut<Components::GlobalMeshLibrary>();
	auto& drawStats = world.get_mut<Components::DrawStats>();

	bool useMeshletReorderedVertices = getMeshShadersEnabled();

	if (meshInstances) {
		for (auto& meshInstance : meshInstances->meshInstances) {

			if (meshInstance->HasSkin()) {
				meshInstance->SetCurrentSkeletonManager(m_managerInterface.GetSkeletonManager());
				auto skinInst = meshInstance->GetSkin();
				m_managerInterface.GetSkeletonManager()->AcquireSkinningInstance(skinInst);
				meshInstance->SetSkinningInstanceSlot(skinInst->GetSkinningInstanceSlot());
				if (skinInst->GetAnimationCount() > 0u) {
					skinInst->SetAnimation(0); // TODO: Animation selection
				}
				meshInstance->SyncSkinningStateFromSkeleton();
			}

			// Increment material usage count
			meshInstance->GetMesh()->material->EnsureTexturesUploaded(*m_managerInterface.GetTextureFactory());
			m_managerInterface.GetMaterialManager()->IncrementMaterialUsageCount(*meshInstance->GetMesh()->material);
			auto materialDataIndex = m_managerInterface.GetMaterialManager()->GetMaterialSlot(meshInstance->GetMesh()->material->GetMaterialID());
			meshInstance->GetMesh()->SetMaterialDataIndex(materialDataIndex);

			// Register mesh if not already present
			if (!globalMeshLibrary.meshes.contains(meshInstance->GetMesh()->GetGlobalID())) {
				globalMeshLibrary.meshes[meshInstance->GetMesh()->GetGlobalID()] = meshInstance->GetMesh();
				m_managerInterface.GetMeshManager()->AddMesh(meshInstance->GetMesh(), useMeshletReorderedVertices);
			}
			m_managerInterface.GetMeshManager()->AddMeshInstance(meshInstance.get(), useMeshletReorderedVertices);

			// Update draw stats and indirect workload counts
            auto& mesh = *meshInstance->GetMesh();
            ForEachMeshDrawWorkload(mesh, [&](const DrawWorkloadKey& workloadKey) {
                if (drawStats.numDrawsPerTechnique.find(workloadKey) == drawStats.numDrawsPerTechnique.end()) {
                    drawStats.numDrawsPerTechnique[workloadKey] = 0;
                }
                drawStats.numDrawsPerTechnique[workloadKey]++;

                m_managerInterface.GetIndirectCommandBufferManager()->RegisterWorkload(workloadKey);
                m_managerInterface.GetIndirectCommandBufferManager()->UpdateBuffersForWorkload(
                    workloadKey,
                    drawStats.numDrawsPerTechnique[workloadKey]);
            });
			drawStats.numDrawsInScene++;
		
		}
	}
}

void Scene::ActivateLight(flecs::entity& entity) {
}

void Scene::ActivateCamera(flecs::entity& entity) {
	entity.add<Components::PrimaryCamera>();
}

void Scene::ProcessEntitySkins(bool overrideExistingSkins) {
	auto& world = GetSceneWorld();
	std::vector<flecs::entity> renderables;
	VisitSceneDescendants(ECSSceneRoot, [&](flecs::entity entity) {
		if (entity.has<Components::MeshInstances>()) {
			renderables.push_back(entity);
		}
	});
	world.defer_begin();
	for (auto entity : renderables) {
		auto oldMeshInstances = entity.try_get<Components::MeshInstances>();
		if (!oldMeshInstances) {
			continue;
		}

		Components::MeshInstances meshInstances;
		meshInstances.generation = oldMeshInstances->generation + 1;

		bool addSkin = false;
		for (const auto& meshInstance : oldMeshInstances->meshInstances) {
			auto rebuiltInstance = MeshInstance::CreateUnique(meshInstance->GetMesh());
			if (rebuiltInstance->HasSkin()) {
				addSkin = true;
			}
			meshInstances.meshInstances.push_back(std::move(rebuiltInstance));
		}

		entity.set<Components::MeshInstances>(meshInstances);
		if (addSkin) {
			entity.add<Components::Skinned>();
		}
		else if (overrideExistingSkins || entity.has<Components::Skinned>()) {
			entity.remove<Components::Skinned>();
		}
	}
	world.defer_end();
}

flecs::entity Scene::CreateRenderableEntityECS(const std::vector<std::shared_ptr<Mesh>>& meshes, std::wstring name) {
	auto& world = GetSceneWorld();
	flecs::entity entity = world.entity();
	entity.child_of(ECSSceneRoot)
		.set_name((ws2s(name) + "_" + std::to_string(entity.id())).c_str())
		.set<Components::Rotation>({ 0, 0, 0, 1 })
		.set<Components::Position>({ 0, 0, 0 })
		.set<Components::Scale>({ 1, 1, 1 })
		.set<Components::Matrix>(DirectX::XMMatrixIdentity())
		.set<Components::Name>(ws2s(name));
	AssignStableSceneID(entity);
	Components::MeshInstances meshInstances;
    for (auto& mesh : meshes) {
		bool skinned = mesh->HasBaseSkin();
		if (skinned) {
			entity.add<Components::Skinned>();
		}
		meshInstances.meshInstances.push_back(std::move(MeshInstance::CreateUnique(mesh)));
		if (skinned) {
			auto skeleton = mesh->GetBaseSkin()->CopySkeleton();
			meshInstances.meshInstances.back()->SetSkeleton(skeleton);
			entity.add<Components::Skinned>();
		}
    }
	if (!meshInstances.meshInstances.empty()) {
		entity.set<Components::MeshInstances>(meshInstances);
	}

	// If scene is active, add object & manage meshes
	if (ECSSceneRoot.has<Components::ActiveScene>()) {
		ActivateRenderable(entity);
		entity.add<Components::Active>();
	}

    return entity;
}

flecs::entity Scene::CreateNodeECS(std::wstring name) {
	auto& world = GetSceneWorld();
	flecs::entity entity = world.entity();
	entity.child_of(ECSSceneRoot)
		.set_name((ws2s(name) + "_" + std::to_string(entity.id())).c_str())
		.add<Components::SceneNode>()
		.set<Components::Rotation>({ 0, 0, 0, 1 })
		.set<Components::Position>({ 0, 0, 0 })
		.set<Components::Scale>({ 1, 1, 1 })
		.set<Components::Matrix>(DirectX::XMMatrixIdentity())
		.set<Components::Name>(ws2s(name));
	AssignStableSceneID(entity);
	return entity;

	if (ECSSceneRoot.has<Components::ActiveScene>()) {
		entity.add<Components::Active>();
	}
}

flecs::entity Scene::GetRoot() const {
    return ECSSceneRoot;
}

uint64_t Scene::GetSceneID() const {
	return m_sceneID;
}

void Scene::Update(float elapsedSeconds) {

	for (auto& node : animatedEntitiesByID) {
		auto& entity = node.second;
		AnimationController* animationController = entity.try_get_mut<AnimationController>();
#if defined(_DEBUG)
		if (animationController == nullptr) {
			spdlog::error("AnimationController is null for entity with ID: {}", node.first);
			return;
		}
#endif
	    auto& transform = animationController->GetUpdatedTransform(elapsedSeconds);
		entity.set<Components::Rotation>(transform.rot);
		entity.set<Components::Position>(transform.pos);
		entity.set<Components::Scale>(transform.scale);
	}
	for (auto& child : m_childScenes) {
		child->Update(elapsedSeconds);
	}
}

void Scene::SetCamera(XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) {
		
    if (m_primaryCamera.is_valid()) {

        m_managerInterface.GetIndirectCommandBufferManager()->UnregisterBuffers(m_primaryCamera.id());
    }

	SettingsManager::GetInstance().getSettingSetter<float>("maxShadowDistance")(zFar);


    CameraInfo info;
	auto planes = GetFrustumPlanesPerspective(aspect, fov, zNear, zFar);
	info.view = XMMatrixIdentity();
	info.viewInverse = XMMatrixIdentity();
	info.unjitteredProjection = XMMatrixPerspectiveFovRH(fov, aspect, zFar, zNear);  // Note the reversed near/far for reversed Z
	info.jitteredProjection = info.unjitteredProjection;
	info.viewProjection = DirectX::XMMatrixMultiply(info.view, info.unjitteredProjection);
	info.projectionInverse = XMMatrixInverse(nullptr, info.unjitteredProjection);
	info.prevView = info.view;
	info.prevJitteredProjection = info.jitteredProjection;
	info.prevUnjitteredProjection = info.unjitteredProjection;
	info.positionWorldSpace = { 0.0f, 0.0f, 0.0f, 1.0f };
	info.clippingPlanes[0] = planes[0];
	info.clippingPlanes[1] = planes[1];
	info.clippingPlanes[2] = planes[2];
	info.clippingPlanes[3] = planes[3];
	info.clippingPlanes[4] = planes[4];
	info.clippingPlanes[5] = planes[5];
	info.zFar = zFar;
	info.zNear = zNear;
	info.aspectRatio = aspect;
	info.fov = fov;

	auto& world = GetSceneWorld();
	Components::Camera camera = {};
	camera.fov = fov;
	camera.aspect = aspect;
	camera.zNear = zNear;
	camera.zFar = zFar;
	camera.info = info;
	auto entity = world.entity()
		.set<Components::Camera>(camera)
		.set<Components::Position>({ 0, 0, 0 })
		.set<Components::Rotation>({ 0, 0, 0 })
		.set<Components::Scale>({ 1, 1, 1 })
		.set<Components::Matrix>(DirectX::XMMatrixIdentity())
		.set<Components::Name>("Primary Camera")
		.child_of(ECSSceneRoot);
	AssignStableSceneID(entity);

	if (ECSSceneRoot.has<Components::ActiveScene>()) {
		ActivateCamera(entity);
		entity.add<Components::Active>();
	}

    setDirectionalLightCascadeSplits(calculateCascadeSplits(getNumDirectionalLightCascades(), zNear, getMaxShadowDistance(), 100.f));

	m_primaryCamera = entity;
}

flecs::entity& Scene::GetPrimaryCamera() {
    return m_primaryCamera;
}

bool Scene::HasUsablePrimaryCamera() const {
	return m_primaryCamera
		&& m_primaryCamera.is_alive()
		&& m_primaryCamera.has<Components::Camera>();
}

Components::Position& Scene::GetPrimaryCameraPosition() {
	return m_primaryCamera.get_mut<Components::Position>();
}

Components::Rotation& Scene::GetPrimaryCameraRotation() {
	return m_primaryCamera.get_mut<Components::Rotation>();
}

void Scene::PropagateTransforms() {
	auto& world = GetSceneWorld();

	// Lazy-init helper queries (stored as members so they're destroyed with the Scene)
	if (!m_propagateQueriesBuilt) {
		m_updatedCleanupQuery = world.query_builder<>()
			.with<Components::TransformUpdatedThisFrame>()
			.build();
		m_dirtyQuery = world.query_builder<>()
			.with<Components::TransformDirty>()
			.with<Components::Active>()
			.build();
		m_propagateQueriesBuilt = true;
	}

	// Clear previous frame's TransformUpdatedThisFrame tags
	world.defer_begin();
	m_updatedCleanupQuery.each([](flecs::entity e) {
		e.remove<Components::TransformUpdatedThisFrame>();
	});
	world.defer_end();

	// Propagate TransformDirty from dirty entities to their active descendants
	std::vector<flecs::entity> dirtyRoots;
	m_dirtyQuery.each([&](flecs::entity e) {
		dirtyRoots.push_back(e);
	});

	if (!dirtyRoots.empty()) {
		std::unordered_set<uint64_t> visited;
		for (auto& e : dirtyRoots) {
			visited.insert(e.id());
		}
		world.defer_begin();
		for (auto& e : dirtyRoots) {
			PropagateTransformDirtyToChildren(e, visited);
		}
		world.defer_end();
	}

	// Run all systems (transform system now filters by TransformDirty)
	world.progress();
}

void Scene::PostUpdate() {
	for (auto& child : m_childScenes) {
		child->PostUpdate();
	}
}

std::shared_ptr<Scene> Scene::AppendScene(std::shared_ptr<Scene> scene) {
	if (!scene) {
		return nullptr;
	}
	auto root = scene->GetRoot();

	root.child_of(ECSSceneRoot);
	if (ECSSceneRoot.has<Components::ActiveScene>()) { // If this scene is active, activate the new scene
		scene->Activate(m_managerInterface);
	}
	m_childScenes.push_back(scene);

	return scene;
}

void Scene::MakeResident() {
	auto& world = GetSceneWorld();
	std::vector<flecs::entity> renderables;
	std::vector<flecs::entity> cameras;
	std::vector<flecs::entity> lights;

	VisitSceneDescendants(ECSSceneRoot, [&](flecs::entity entity) {
		if (entity.has<Components::MeshInstances>()) {
			renderables.push_back(entity);
		}
		if (entity.has<Components::Camera>()) {
			cameras.push_back(entity);
		}
		if (entity.has<Components::Light>()) {
			lights.push_back(entity);
		}
	});

	for (auto& entity : renderables) {
		ActivateRenderable(entity);
	}
	for (auto& entity : cameras) {
		ActivateCamera(entity);
	}
	for (auto& entity : lights) {
		ActivateLight(entity);
	}
}

void Scene::MakeNonResident() {
	// TODO
}

Scene::~Scene() {
	MakeNonResident();
}

void activate_hierarchy(flecs::entity src) {

	src.add<Components::Active>();
	src.add<Components::TransformDirty>();

	src.children([&](flecs::entity e) {
		activate_hierarchy(e);
		});
}

void ActivateHierarchy(flecs::entity src) {
	src.world().defer_begin();
	activate_hierarchy(src);
	src.world().defer_end();
}

void Scene::ActivateAllAnimatedEntities() {
	auto& world = GetSceneWorld();
	world.defer_begin();
	for (auto& e : animatedEntitiesByID) {
		auto& entity = e.second;
		entity.add<Components::Active>();
	}
	world.defer_end();
	for (auto & child : m_childScenes) {
		child->ActivateAllAnimatedEntities();
	}
}

void Scene::Activate(ManagerInterface managerInterface) {
	m_managerInterface = managerInterface;

	ActivateHierarchy(ECSSceneRoot);
	ActivateAllAnimatedEntities();

	ECSSceneRoot.add<Components::Active>();

	MakeResident();
}

void recurse_hierarchy(flecs::entity src, flecs::entity dst_parent = {}) {
	if (src.has<Components::SkeletonRoot>()) {
		return; // Skip skeleton roots, they are handled separately
	}
	flecs::entity cloned = src.clone();
	AssignStableSceneID(cloned);

	if (dst_parent.is_alive()) {
		cloned.child_of(dst_parent);
	}

	src.children([&](flecs::entity e) {
		recurse_hierarchy(e, cloned);
		});
}

void CloneHierarchy(flecs::entity src, flecs::entity dst_parent) {
	src.world().defer_begin();
	src.children([&](flecs::entity e) {
		recurse_hierarchy(e, dst_parent);
		});
	src.world().defer_end();
}

std::shared_ptr<Scene> Scene::Clone() const {
	auto newScene = std::make_shared<Scene>();
	newScene->ECSSceneRoot = ECSSceneRoot.clone();
	ReassignStableSceneIDsRecursive(newScene->ECSSceneRoot);
	CloneHierarchy(ECSSceneRoot, newScene->ECSSceneRoot);
	for (auto& childScene : m_childScenes) {
		newScene->m_childScenes.push_back(childScene->Clone());
	}
	newScene->ProcessEntitySkins(true);
	return newScene;
}

void Scene::DisableShadows() {
	auto& world = GetSceneWorld();
	std::vector<flecs::entity> renderables;
	VisitSceneDescendants(ECSSceneRoot, [&](flecs::entity entity) {
		if (entity.has<Components::MeshInstances>()) {
			renderables.push_back(entity);
		}
	});
	world.defer_begin();
	for (auto entity : renderables) {
		entity.add<Components::SkipShadowPass>();
	}
	world.defer_end();
}
