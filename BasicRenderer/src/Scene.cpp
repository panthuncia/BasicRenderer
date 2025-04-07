#include "Scene.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <execution>
#include <flecs.h>

#include "Utilities.h"
#include "SettingsManager.h"
#include "CameraManager.h"
#include "ECSManager.h"
#include "Components.h"
#include "material.h"
#include "ObjectManager.h"
#include "MeshManager.h"
#include "LightManager.h"
#include "IndirectCommandBufferManager.h"

Scene::Scene(){
    getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
    getMaxShadowDistance = SettingsManager::GetInstance().getSettingGetter<float>("maxShadowDistance");
    setDirectionalLightCascadeSplits = SettingsManager::GetInstance().getSettingSetter<std::vector<float>>("directionalLightCascadeSplits");

    // TODO: refactor indirect buffer manager and light manager so that GPU resources are destroyed on MakeNonResident
	//indirectCommandBufferManager = IndirectCommandBufferManager::CreateShared();

    //Initialize ECS scene
    auto& world = ECSManager::GetInstance().GetWorld();
    ECSSceneRoot = world.entity().add<Components::SceneRoot>();
    world.set_pipeline(world.get<Components::GameScene>()->pipeline);
}

flecs::entity Scene::CreateDirectionalLightECS(std::wstring name, XMFLOAT3 color, float intensity, XMFLOAT3 direction){
	return CreateLightECS(name, Components::LightType::Directional, { 0, 0, 0 }, color, intensity, 0, 0, 0, direction);
}

flecs::entity Scene::CreatePointLightECS(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation, float linearAttenuation, float quadraticAttenuation) {
	return CreateLightECS(name, Components::LightType::Point, position, color, intensity, constantAttenuation, linearAttenuation, quadraticAttenuation);
}

flecs::entity Scene::CreateSpotLightECS(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle, float constantAttenuation, float linearAttenuation, float quadraticAttenuation) {
	return CreateLightECS(name, Components::LightType::Spot, position, color, intensity, constantAttenuation, linearAttenuation, quadraticAttenuation, direction, innerConeAngle, outerConeAngle);
}

flecs::entity Scene::CreateLightECS(std::wstring name, Components::LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation, float linearAttenuation, float quadraticAttenuation, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle) {
	auto& world = ECSManager::GetInstance().GetWorld();
	float range = 5.0f;
	flecs::entity entity = world.entity();
	entity.child_of(ECSSceneRoot)
		.set<Components::Light>({ type, color, XMFLOAT3(constantAttenuation, linearAttenuation, quadraticAttenuation), range })
		.set<Components::Position>(position);

	if (direction.x != 0 || direction.y != 0 || direction.z || 0) {
		entity.set<Components::Rotation>(QuaternionFromAxisAngle(direction));
	}
	
	LightInfo lightInfo;
	lightInfo.type = type;
	lightInfo.posWorldSpace = XMLoadFloat3(&position);
	lightInfo.color = XMVector3Normalize(XMLoadFloat3(&color));
	lightInfo.color *= intensity;
	float nearPlane = 0.01;
	float farPlane = range;
	lightInfo.attenuation = XMVectorSet(constantAttenuation, linearAttenuation, quadraticAttenuation, 0);
	lightInfo.dirWorldSpace = XMLoadFloat3(&direction);
	lightInfo.innerConeAngle = cos(innerConeAngle);
	lightInfo.outerConeAngle = cos(outerConeAngle);
	lightInfo.shadowViewInfoIndex = -1;
	lightInfo.nearPlane = nearPlane;
	lightInfo.farPlane = farPlane;

	if (ECSSceneRoot.has<Components::ActiveScene>()) {
		auto viewInfo = m_managerInterface.GetLightManager()->AddLight(&lightInfo, entity.id());
		entity.set<Components::LightViewInfo>({ viewInfo });
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

	std::vector<std::array<ClippingPlane, 6>> frustrumPlanes;
	switch (type) {
	case Components::LightType::Directional:
		break; // Directional is special-cased, frustrums are in world space, calculated during cascade setup
	case Components::LightType::Spot: {
		frustrumPlanes.push_back(GetFrustumPlanesPerspective(1.0f, outerConeAngle * 2, nearPlane, farPlane));
		entity.set<Components::FrustrumPlanes>({ frustrumPlanes });
		break;
	case Components::LightType::Point: {
		for (int i = 0; i < 6; i++) {
			frustrumPlanes.push_back(GetFrustumPlanesPerspective(1.0f, XM_PI / 2, nearPlane, farPlane)); // TODO: All of these are the same.
		}
		entity.set<Components::FrustrumPlanes>({ frustrumPlanes });
		break;
	}
	}
	}
	
	return entity;
}

flecs::entity Scene::CreateRenderableEntityECS(const std::vector<std::shared_ptr<Mesh>>& meshes, std::wstring name) {
	auto& world = ECSManager::GetInstance().GetWorld();
	flecs::entity entity = world.entity();
	PerObjectCB buffer;
	entity.child_of(ECSSceneRoot)
		.set_name((ws2s(name)+"_"+std::to_string(entity.id())).c_str())
		.set<Components::RenderableObject>({buffer})
        .set<Components::Rotation>({0, 0, 0, 1 })
        .set<Components::Position>({ 0, 0, 0 })
        .set<Components::Scale>({ 1, 1, 1 });
	Components::OpaqueMeshInstances opaqueMeshInstances;
	Components::AlphaTestMeshInstances alphaTestMeshInstances;
	Components::BlendMeshInstances blendMeshInstances;
    for (auto& mesh : meshes) {
		bool skinned = mesh->HasBaseSkin();
		if (skinned) {
			entity.add<Components::Skinned>();
		}
        switch (mesh->material->m_blendState) {
		case BlendState::BLEND_STATE_OPAQUE: {
			opaqueMeshInstances.meshInstances.push_back(std::move(MeshInstance::CreateUnique(mesh)));
			if (skinned) {
				entity.add<Components::OpaqueSkinned>();
			}
			break;
		}
		case BlendState::BLEND_STATE_MASK: {
			alphaTestMeshInstances.meshInstances.push_back(std::move(MeshInstance::CreateUnique(mesh)));
			if (skinned) {
				entity.add<Components::AlphaTestSkinned>();
			}
			break;
		}
		case BlendState::BLEND_STATE_BLEND: {
			blendMeshInstances.meshInstances.push_back(std::move(MeshInstance::CreateUnique(mesh)));
			if (skinned) {
				entity.add<Components::BlendSkinned>();
			}
			break;
		}
        }
    }
	if (!opaqueMeshInstances.meshInstances.empty()) {
		entity.set<Components::OpaqueMeshInstances>(opaqueMeshInstances);
	}
	if (!alphaTestMeshInstances.meshInstances.empty()) {
		entity.set<Components::AlphaTestMeshInstances>(alphaTestMeshInstances);
	}
	if (!blendMeshInstances.meshInstances.empty()) {
		entity.set<Components::BlendMeshInstances>(blendMeshInstances);
	}

	// If scene is active, add object & manage meshes
	if (ECSSceneRoot.has<Components::ActiveScene>()) {
		auto drawInfo = m_managerInterface.GetObjectManager()->AddObject(buffer, &opaqueMeshInstances, &alphaTestMeshInstances, &blendMeshInstances);
		entity.set<Components::ObjectDrawInfo>(drawInfo);

		auto globalMeshLibrary = world.get_mut<Components::GlobalMeshLibrary>();
		auto drawStats = world.get_mut<Components::DrawStats>();
		if (drawInfo.opaque.has_value()) {
			//e.add<Components::OpaqueMeshInstances>(drawInfo.opaque.value());
			for (auto& meshInstance : opaqueMeshInstances.meshInstances) {
				if (!globalMeshLibrary->meshes.contains(meshInstance->GetMesh()->GetGlobalID())) {
					globalMeshLibrary->meshes[meshInstance->GetMesh()->GetGlobalID()] = meshInstance->GetMesh();
					m_managerInterface.GetMeshManager()->AddMesh(meshInstance->GetMesh(), MaterialBuckets::Opaque);
				}
				m_managerInterface.GetMeshManager()->AddMeshInstance(meshInstance.get());
			}
			drawStats->numOpaqueDraws += drawInfo.opaque.value().indices.size();
			drawStats->numDrawsInScene += drawInfo.opaque.value().indices.size();
		}
		if (drawInfo.alphaTest.has_value()) {
			//e.add<Components::AlphaTestMeshInstances>(drawInfo.alphaTest.value());
			for (auto& meshInstance : alphaTestMeshInstances.meshInstances) {
				if (!globalMeshLibrary->meshes.contains(meshInstance->GetMesh()->GetGlobalID())) {
					globalMeshLibrary->meshes[meshInstance->GetMesh()->GetGlobalID()] = meshInstance->GetMesh();
					m_managerInterface.GetMeshManager()->AddMesh(meshInstance->GetMesh(), MaterialBuckets::AlphaTest);
				}
				m_managerInterface.GetMeshManager()->AddMeshInstance(meshInstance.get());
			}
			drawStats->numAlphaTestDraws += drawInfo.alphaTest.value().indices.size();
			drawStats->numDrawsInScene += drawInfo.alphaTest.value().indices.size();
		}
		if (drawInfo.blend.has_value()) {
			//e.add<Components::BlendMeshInstances>(drawInfo.blend.value());
			for (auto& meshInstance : blendMeshInstances.meshInstances) {
				if (!globalMeshLibrary->meshes.contains(meshInstance->GetMesh()->GetGlobalID())) {
					globalMeshLibrary->meshes[meshInstance->GetMesh()->GetGlobalID()] = meshInstance->GetMesh();
					m_managerInterface.GetMeshManager()->AddMesh(meshInstance->GetMesh(), MaterialBuckets::Blend);
				}
				m_managerInterface.GetMeshManager()->AddMeshInstance(meshInstance.get());
			}
			drawStats->numBlendDraws += drawInfo.blend.value().indices.size();
			drawStats->numDrawsInScene += drawInfo.blend.value().indices.size();
		}
		if (drawInfo.opaque.has_value()) {
			m_managerInterface.GetIndirectCommandBufferManager()->UpdateBuffersForBucket(MaterialBuckets::Opaque, drawStats->numOpaqueDraws);
		}
		if (drawInfo.alphaTest.has_value()) {
			m_managerInterface.GetIndirectCommandBufferManager()->UpdateBuffersForBucket(MaterialBuckets::AlphaTest, drawStats->numAlphaTestDraws);
		}
		if (drawInfo.blend.has_value()) {
			m_managerInterface.GetIndirectCommandBufferManager()->UpdateBuffersForBucket(MaterialBuckets::Blend, drawStats->numBlendDraws);
		}
	}

    return entity;
}

flecs::entity Scene::CreateNodeECS(std::wstring name) {
	auto& world = ECSManager::GetInstance().GetWorld();
	flecs::entity entity = world.entity();
	entity.child_of(ECSSceneRoot)
		.set_name((ws2s(name)+"_"+std::to_string(entity.id())).c_str())
		.add<Components::SceneNode>()
		.set<Components::Rotation>({ 0, 0, 0, 1 })
		.set<Components::Position>({ 0, 0, 0 })
		.set<Components::Scale>({ 1, 1, 1 });
	return entity;
}

flecs::entity Scene::GetRoot() {
    return ECSSceneRoot;
}

void Scene::Update() {
    auto currentTime = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = currentTime - lastUpdateTime;
    lastUpdateTime = currentTime;

    //std::for_each(std::execution::par, animatedEntitiesByID.begin(), animatedEntitiesByID.end(), 
    //    [elapsed = elapsed_seconds.count()](auto& node) {
    //        node.second->animationController->update(elapsed);
    //    });
	for (auto& node : animatedEntitiesByID) {
		auto& entity = node.second;
		AnimationController* animationController = entity.get_mut<AnimationController>();
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

    //this->sceneRoot.Update();
    for (auto& skeleton : animatedSkeletons) {
        skeleton->UpdateTransforms();
    }
    PostUpdate();
}

void Scene::SetCamera(XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) {
		
    if (m_primaryCamera.is_valid()) {

        m_managerInterface.GetIndirectCommandBufferManager()->UnregisterBuffers(m_primaryCamera.id());
        m_pPrimaryCameraOpaqueIndirectCommandBuffer = nullptr;
		m_pPrimaryCameraAlphaTestIndirectCommandBuffer = nullptr;
		m_pPrimaryCameraBlendIndirectCommandBuffer = nullptr;

		m_pCameraManager->RemoveCamera(pCamera->GetCameraBufferView());
		pCamera->SetCameraBufferView(nullptr);
    }

    //pCamera = std::make_shared<Camera>(L"MainCamera", lookAt, up, fov, aspect, zNear, zFar);
    CameraInfo info;
	auto planes = GetFrustumPlanesPerspective(aspect, fov, zNear, zFar);
	info.view = XMMatrixIdentity();
	info.projection = XMMatrixPerspectiveFovRH(fov, aspect, zNear, zFar);
	info.viewProjection = info.projection;
	info.clippingPlanes[0] = planes[0];
	info.clippingPlanes[1] = planes[1];
	info.clippingPlanes[2] = planes[2];
	info.clippingPlanes[3] = planes[3];
	info.clippingPlanes[4] = planes[4];
	info.clippingPlanes[5] = planes[5];

	auto cameraBufferView = m_managerInterface.GetCameraManager()->AddCamera(info);

	auto& world = ECSManager::GetInstance().GetWorld();
	auto entity = world.entity()
		.set<Components::Camera>({info})
		.set<Components::CameraBufferView>(cameraBufferView)
		.set<Components::Position>({0, 0, 0})
		.set<Components::Rotation>({0, 0, 0})
		.set<Components::Scale>({0, 0, 0})
		.set<Components::Matrix>({});

    setDirectionalLightCascadeSplits(calculateCascadeSplits(getNumDirectionalLightCascades(), zNear, getMaxShadowDistance(), 100.f));
	if (lightManager != nullptr) {
		lightManager->SetCurrentCamera(pCamera.get());
	}
    AddNode(pCamera);
    m_pPrimaryCameraOpaqueIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(pCamera->GetLocalID(), MaterialBuckets::Opaque);
	m_pPrimaryCameraAlphaTestIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(pCamera->GetLocalID(), MaterialBuckets::AlphaTest);
	m_pPrimaryCameraBlendIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(pCamera->GetLocalID(), MaterialBuckets::Blend);
}

flecs::entity& Scene::GetPrimaryCamera() {
    return m_primaryCamera;
}

void Scene::AddSkeleton(std::shared_ptr<Skeleton> skeleton) {
    skeletons.push_back(skeleton);
    if (skeleton->animations.size() > 0) {
        skeleton->SetAnimation(0);
        animatedSkeletons.push_back(skeleton);
    }
	//auto entity = skeleton->GetRoot();
	//entity.child_of(ECSSceneRoot);
	for (auto& node : skeleton->m_bones) {
		animatedEntitiesByID[node.id()] = node;
	}
}

void Scene::PostUpdate() {
}

std::shared_ptr<SceneNode> Scene::AppendScene(Scene& scene) {
	auto root = scene.GetRoot();
	root.child_of(ECSSceneRoot);
      return nullptr;
}

void Scene::MakeResident() {

}

void Scene::MakeNonResident() {

}

Scene::~Scene() {
	MakeNonResident();
}

void Scene::Activate(ManagerInterface managerInterface) {
	m_managerInterface = managerInterface;
	MakeResident();
}

std::shared_ptr<DynamicGloballyIndexedResource> Scene::GetPrimaryCameraOpaqueIndirectCommandBuffer() {
	return m_pPrimaryCameraOpaqueIndirectCommandBuffer;
}

std::shared_ptr<DynamicGloballyIndexedResource> Scene::GetPrimaryCameraAlphaTestIndirectCommandBuffer() {
	return m_pPrimaryCameraAlphaTestIndirectCommandBuffer;
}

std::shared_ptr<DynamicGloballyIndexedResource> Scene::GetPrimaryCameraBlendIndirectCommandBuffer() {
	return m_pPrimaryCameraBlendIndirectCommandBuffer;
}