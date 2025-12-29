#include "Scene/Scene.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <execution>
#include <flecs.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include "Managers/Singletons/ECSManager.h"
#include "Scene/Components.h"
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

std::atomic<uint64_t> Scene::globalSceneCount = 0;

Scene::Scene(){
	m_sceneID = globalSceneCount.fetch_add(1, std::memory_order_relaxed);

    getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
    getMaxShadowDistance = SettingsManager::GetInstance().getSettingGetter<float>("maxShadowDistance");
    setDirectionalLightCascadeSplits = SettingsManager::GetInstance().getSettingSetter<std::vector<float>>("directionalLightCascadeSplits");
	getMeshShadersEnabled = SettingsManager::GetInstance().getSettingGetter<bool>("enableMeshShader");

    //Initialize ECS scene
    auto& world = ECSManager::GetInstance().GetWorld();
    ECSSceneRoot = world.entity().add<Components::SceneRoot>()
		.set<Components::Position>({0, 0, 0})
		.set<Components::Rotation>({0, 0, 0, 1})
		.set<Components::Scale>({1, 1, 1})
		.set<Components::Matrix>(DirectX::XMMatrixIdentity())
		.set<Components::Name>("Scene Root");
	ECSSceneRoot = ECSSceneRoot;
    world.set_pipeline(world.get<Components::GameScene>().pipeline);

	m_renderResSubscription = SettingsManager::GetInstance().addObserver<DirectX::XMUINT2>("renderResolution", [this](const DirectX::XMUINT2& renderRes) {
		UpdateMainCameraDepths();
		});
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
	auto& world = ECSManager::GetInstance().GetWorld();
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
	auto& world = ECSManager::GetInstance().GetWorld();

	auto& buffer = entity.get_mut<Components::RenderableObject>().perObjectCB;
	auto meshInstances = entity.try_get<Components::MeshInstances>();
	//auto alphaTestMeshInstances = entity.try_get<Components::AlphaTestMeshInstances>();
	//auto blendMeshInstances = entity.try_get<Components::BlendMeshInstances>();

	auto& globalMeshLibrary = world.get_mut<Components::GlobalMeshLibrary>();
	auto& drawStats = world.get_mut<Components::DrawStats>();

	bool useMeshletReorderedVertices = getMeshShadersEnabled();

	if (meshInstances) {
		Components::PerPassMeshes perPassMeshes;
		//e.add<Components::OpaqueMeshInstances>(drawInfo.opaque.value());
		for (auto& meshInstance : meshInstances->meshInstances) {

			if (meshInstance->HasSkin()) {
				auto skinInst = meshInstance->GetSkin();
				m_managerInterface.GetSkeletonManager()->AcquireSkinningInstance(skinInst);
				meshInstance->SetSkinningInstanceSlot(skinInst->GetSkinningInstanceSlot());
			}

			// Increment material usage count
			m_managerInterface.GetMaterialManager()->IncrementMaterialUsageCount(*meshInstance->GetMesh()->material);
			auto materialDataIndex = m_managerInterface.GetMaterialManager()->GetMaterialSlot(meshInstance->GetMesh()->material->GetMaterialID());
			meshInstance->GetMesh()->SetMaterialDataIndex(materialDataIndex);

			// Register mesh if not already present
			if (!globalMeshLibrary.meshes.contains(meshInstance->GetMesh()->GetGlobalID())) {
				globalMeshLibrary.meshes[meshInstance->GetMesh()->GetGlobalID()] = meshInstance->GetMesh();
				m_managerInterface.GetMeshManager()->AddMesh(meshInstance->GetMesh(), useMeshletReorderedVertices);
			}
			m_managerInterface.GetMeshManager()->AddMeshInstance(meshInstance.get(), useMeshletReorderedVertices);

			// Update draw stats
			auto& technique = meshInstance->GetMesh()->material->Technique();
			if (drawStats.numDrawsPerTechnique.find(technique.compileFlags) == drawStats.numDrawsPerTechnique.end()) {
				drawStats.numDrawsPerTechnique[technique.compileFlags] = 0;
			}
			drawStats.numDrawsPerTechnique[technique.compileFlags]++; // TODO: Make a better system for managing things that depend on which material objects use, as they may change
			drawStats.numDrawsInScene++;

			// Update indirect draw counts
			m_managerInterface.GetIndirectCommandBufferManager()->RegisterTechnique(technique); // Ensure technique is registered
			m_managerInterface.GetIndirectCommandBufferManager()->UpdateBuffersForTechnique(technique, drawStats.numDrawsPerTechnique[technique.compileFlags]);
		
			// Register material passes in ECS
			for (auto& pass : technique.passes) {
				auto passEntity = ECSManager::GetInstance().GetRenderPhaseEntity(pass);
				entity.add<Components::ParticipatesInPass>(passEntity);
				if (!perPassMeshes.meshesByPass.contains(pass.hash)) {
					perPassMeshes.meshesByPass[pass.hash] = std::vector<std::shared_ptr<MeshInstance>>();
				}
				perPassMeshes.meshesByPass[pass.hash].push_back(meshInstance);
			}

			entity.set<Components::PerPassMeshes>(perPassMeshes);
		}
	}

	auto drawInfo = m_managerInterface.GetObjectManager()->AddObject(buffer, meshInstances);
	entity.set<Components::ObjectDrawInfo>(drawInfo);
	buffer.normalMatrixBufferIndex = drawInfo.normalMatrixIndex;
}

void Scene::ActivateLight(flecs::entity& entity) {
	auto lightInfo = entity.get<Components::Light>();
	auto newInfo = Components::Light(lightInfo);
	AddLightReturn addInfo = m_managerInterface.GetLightManager()->AddLight(&newInfo.lightInfo, entity.id());
	entity.set<Components::LightViewInfo>({ addInfo.lightViewInfo });
	if (addInfo.shadowMap.has_value()) {
		entity.set<Components::DepthMap>({ addInfo.shadowMap.value() });
		newInfo.lightInfo.shadowMapIndex = addInfo.shadowMap.value().depthMap->GetSRVInfo(0).slot.index;
		newInfo.lightInfo.shadowSamplerIndex = Sampler::GetDefaultShadowSampler()->GetDescriptorIndex();
		newInfo.lightInfo.shadowViewInfoIndex = addInfo.lightViewInfo.viewInfoBufferIndex;
		m_managerInterface.GetLightManager()->UpdateLightBufferView(addInfo.lightViewInfo.lightBufferView.get(), newInfo.lightInfo);
		entity.set<Components::Light>(newInfo);
	}
	if (addInfo.frustumPlanes.has_value()) {
		entity.set<Components::FrustumPlanes>({ addInfo.frustumPlanes.value() });
	}
}

void Scene::ActivateCamera(flecs::entity& entity) {
	auto camera = entity.get<Components::Camera>();

	Components::Camera newCameraInfo = {};
	newCameraInfo = camera;
	auto screenRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
	auto depth = CreateDepthMapComponent(screenRes.x, screenRes.y, 1, false);
	newCameraInfo.info.numDepthMips = NumMips(screenRes.x, screenRes.y);
	newCameraInfo.info.depthResX = screenRes.x;
	newCameraInfo.info.depthResY = screenRes.y;
	unsigned int linearDepthX = screenRes.x;
	unsigned int linearDepthY = screenRes.y;
	unsigned int paddedLinearDepthX = depth.linearDepthMap->GetInternalWidth();
	unsigned int paddedLinearDepthY = depth.linearDepthMap->GetInternalHeight();
	newCameraInfo.info.uvScaleToNextPowerOfTwo = { static_cast<float>(linearDepthX) / paddedLinearDepthX, static_cast<float>(linearDepthY) / paddedLinearDepthY };

	entity.set<Components::Camera> (newCameraInfo);

	auto renderView = m_managerInterface.GetViewManager()->CreateView(newCameraInfo.info, ViewFlags::PrimaryCamera());
	m_managerInterface.GetViewManager()->AttachDepth(renderView, depth.depthMap, depth.linearDepthMap);
	entity.set<Components::RenderViewRef>({renderView});
	entity.set<Components::DepthMap>(depth);

	entity.add<Components::PrimaryCamera>();
}

void Scene::UpdateMainCameraDepths() {
	if (m_primaryCamera.is_alive()) {
		ActivateCamera(m_primaryCamera);
	}
}

void Scene::ProcessEntitySkins(bool overrideExistingSkins) {
	auto& world = ECSManager::GetInstance().GetWorld();
	auto query = world.query_builder<>().with<Components::RenderableObject>()
		.with(flecs::ChildOf, ECSSceneRoot).self().parent()
		.build();
	std::vector<std::shared_ptr<Skeleton>> skeletonsToAdd;
	world.defer_begin();
	query.each([&](flecs::entity entity) {
		auto oldMeshInstances = entity.try_get<Components::MeshInstances>();

		// Discard old instances and add new ones
		if (oldMeshInstances) {
			Components::MeshInstances meshInstances;
			for (auto& meshInstance : oldMeshInstances->meshInstances) {
				meshInstances.meshInstances.push_back(std::move(MeshInstance::CreateUnique(meshInstance->GetMesh())));
			}
			entity.set<Components::MeshInstances>(meshInstances);
		}

		if (oldMeshInstances) {
			bool addSkin = false;
			for (auto& meshInstance : oldMeshInstances->meshInstances) {
				if (meshInstance->GetMesh()->HasBaseSkin() && (!meshInstance->HasSkin() || overrideExistingSkins)) {
					auto skinInst = meshInstance->GetMesh()->GetBaseSkin()->CopySkeleton();   // runtime instance
					meshInstance->SetSkeleton(skinInst);
					addSkin = true;
				}
			}
			if (addSkin) {
				entity.add<Components::Skinned>();
			}
		}
		});
	world.defer_end();
}

flecs::entity Scene::CreateRenderableEntityECS(const std::vector<std::shared_ptr<Mesh>>& meshes, std::wstring name) {
	auto& world = ECSManager::GetInstance().GetWorld();
	flecs::entity entity = world.entity();
	PerObjectCB buffer = {};
	entity.child_of(ECSSceneRoot)
		.set_name((ws2s(name) + "_" + std::to_string(entity.id())).c_str())
		.set<Components::RenderableObject>({ buffer })
		.set<Components::Rotation>({ 0, 0, 0, 1 })
		.set<Components::Position>({ 0, 0, 0 })
		.set<Components::Scale>({ 1, 1, 1 })
		.set<Components::Matrix>(DirectX::XMMatrixIdentity())
		.set<Components::Name>(ws2s(name));
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
	auto& world = ECSManager::GetInstance().GetWorld();
	flecs::entity entity = world.entity();
	entity.child_of(ECSSceneRoot)
		.set_name((ws2s(name) + "_" + std::to_string(entity.id())).c_str())
		.add<Components::SceneNode>()
		.set<Components::Rotation>({ 0, 0, 0, 1 })
		.set<Components::Position>({ 0, 0, 0 })
		.set<Components::Scale>({ 1, 1, 1 })
		.set<Components::Matrix>(DirectX::XMMatrixIdentity())
		.set<Components::Name>(ws2s(name));
	return entity;

	if (ECSSceneRoot.has<Components::ActiveScene>()) {
		entity.add<Components::Active>();
	}
}

flecs::entity Scene::GetRoot() const {
    return ECSSceneRoot;
}

void Scene::Update() {
    auto currentTime = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsed_seconds = currentTime - lastUpdateTime;
    lastUpdateTime = currentTime;

	for (auto& node : animatedEntitiesByID) {
		auto& entity = node.second;
		AnimationController* animationController = entity.try_get_mut<AnimationController>();
#if defined(_DEBUG)
		if (animationController == nullptr) {
			spdlog::error("AnimationController is null for entity with ID: {}", node.first);
			return;
		}
#endif
	    auto& transform = animationController->GetUpdatedTransform(elapsed_seconds.count());
		entity.set<Components::Rotation>(transform.rot);
		entity.set<Components::Position>(transform.pos);
		entity.set<Components::Scale>(transform.scale);
	}

	m_managerInterface.GetSkeletonManager()->UpdateAllDirtyInstances();

	for (auto& scene : m_childScenes) {
		scene->Update();
	}
    PostUpdate();
}

void Scene::SetDepthMap(Components::DepthMap depthMap) {
	m_primaryCameraDepthMap = depthMap;
	if (m_primaryCamera.is_valid()) {
		m_primaryCamera.set<Components::DepthMap>(depthMap);
	}
}

void Scene::SetCamera(XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) {
		
    if (m_primaryCamera.is_valid()) {

        m_managerInterface.GetIndirectCommandBufferManager()->UnregisterBuffers(m_primaryCamera.id());

		m_managerInterface.GetViewManager()->DestroyView(m_primaryCamera.get<Components::RenderViewRef>().viewID);
    }

	SettingsManager::GetInstance().getSettingSetter<float>("maxShadowDistance")(zFar);


    CameraInfo info;
	auto planes = GetFrustumPlanesPerspective(aspect, fov, zNear, zFar);
	info.view = XMMatrixIdentity();
	info.unjitteredProjection = XMMatrixPerspectiveFovRH(fov, aspect, zNear, zFar);
	info.viewProjection = DirectX::XMMatrixMultiply(info.view, info.unjitteredProjection);
	info.projectionInverse = XMMatrixInverse(nullptr, info.unjitteredProjection);
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

	auto& world = ECSManager::GetInstance().GetWorld();
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
		.set<Components::DepthMap>({ m_primaryCameraDepthMap })
		.child_of(ECSSceneRoot);

	if (ECSSceneRoot.has<Components::ActiveScene>()) {
		ActivateCamera(entity);
		entity.add<Components::Active>();
	}

    setDirectionalLightCascadeSplits(calculateCascadeSplits(getNumDirectionalLightCascades(), zNear, getMaxShadowDistance(), 100.f));
	if (m_managerInterface.GetLightManager() != nullptr) {
		m_managerInterface.GetLightManager()->SetCurrentCamera(entity);
	}

	m_primaryCamera = entity;
}

flecs::entity& Scene::GetPrimaryCamera() {
    return m_primaryCamera;
}

void Scene::PostUpdate() {
}

std::shared_ptr<Scene> Scene::AppendScene(std::shared_ptr<Scene> scene) {

	auto root = scene->GetRoot();

	root.child_of(ECSSceneRoot);
	if (ECSSceneRoot.has<Components::ActiveScene>()) { // If this scene is active, activate the new scene
		scene->Activate(m_managerInterface);
	}
	m_childScenes.push_back(scene);

	return scene;
}

void Scene::MakeResident() {
	auto& world = ECSManager::GetInstance().GetWorld();
	world.defer_begin();
	auto renderableQuery =world.query_builder<>()
		.with<Components::RenderableObject>()
		.with(flecs::ChildOf, ECSSceneRoot).self().parent()
		.build();
	renderableQuery.each([&](flecs::entity entity) {
		ActivateRenderable(entity);
		});

	auto camQuery = world.query_builder<>()
		.with<Components::Camera>()
		.with(flecs::ChildOf, ECSSceneRoot).self().parent()
		.build();
	camQuery.each([&](flecs::entity entity) {
		ActivateCamera(entity);
		});

	auto lightQuery = world.query_builder<>()
		.with<Components::Light>()
		.with(flecs::ChildOf, ECSSceneRoot).self().parent()
		.build();
	lightQuery.each([&](flecs::entity entity) {
		ActivateLight(entity);
		});
	world.defer_end();
}

void Scene::MakeNonResident() {
	// TODO
}

Scene::~Scene() {
	MakeNonResident();
}

void activate_hierarchy(flecs::entity src) {

	src.add<Components::Active>();

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
	auto& world = ECSManager::GetInstance().GetWorld();
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
	CloneHierarchy(ECSSceneRoot, newScene->ECSSceneRoot);
	for (auto& childScene : m_childScenes) {
		newScene->m_childScenes.push_back(childScene->Clone());
	}
	newScene->ProcessEntitySkins(true);
	return newScene;
}

void Scene::DisableShadows() {
	auto& world = ECSManager::GetInstance().GetWorld();
	world.defer_begin();
	auto query = world.query_builder<>()
		.with<Components::RenderableObject>()
		.with(flecs::ChildOf, ECSSceneRoot).self().parent()
		.build();
	query.each([&](flecs::entity entity) {
		entity.add<Components::SkipShadowPass>();
		});
	world.defer_end();
}