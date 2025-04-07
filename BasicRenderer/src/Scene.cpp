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

UINT Scene::AddObject(std::shared_ptr<RenderableObject> object) {
 //   numObjects++;
	//object->SetLocalID(nextNodeID);
 //   objectsByName[object->m_name] = object;
 //   objectsByID[nextNodeID] = object;
 //   nextNodeID++;

 //   if (object->parent == nullptr) {
 //       sceneRoot.AddChild(object);
 //   }
	//for (auto& mesh : object->GetOpaqueMeshes()) {
 //       m_numDrawsInScene++;
	//	m_numOpaqueDraws++;
 //       auto meshGlobaId = mesh->GetMesh()->GetGlobalID();
	//	if (meshManager != nullptr) { // If mesh manager exists, this scene is active and we want to add the mesh immediately
 //           if (!meshesByID.contains(meshGlobaId) || meshesByID[meshGlobaId] != mesh->GetMesh()) {
 //               meshManager->AddMesh(mesh->GetMesh(), MaterialBuckets::Opaque);
 //           }
 //           meshManager->AddMeshInstance(mesh.get());
 //           if (mesh->GetMesh()->HasBaseSkin()) {
 //               auto skeletonCopy = mesh->GetMesh()->GetBaseSkin()->CopySkeleton();
	//			AddSkeleton(skeletonCopy);
	//			mesh->SetSkeleton(skeletonCopy);
 //           }
 //       }
 //       meshesByID[meshGlobaId] = mesh->GetMesh();
	//}
 //   for (auto& mesh : object->GetAlphaTestMeshes()) {
 //       m_numDrawsInScene++;
	//	m_numAlphaTestDraws++;
 //       auto meshGlobaId = mesh->GetMesh()->GetGlobalID();
 //       if (meshManager != nullptr) {
 //           if (!meshesByID.contains(meshGlobaId) || meshesByID[meshGlobaId] != mesh->GetMesh()) {
 //               meshManager->AddMesh(mesh->GetMesh(), MaterialBuckets::AlphaTest);
 //           }
 //           meshManager->AddMeshInstance(mesh.get());
 //           if (mesh->GetMesh()->HasBaseSkin()) {
 //               auto skeletonCopy = mesh->GetMesh()->GetBaseSkin()->CopySkeleton();
 //               AddSkeleton(skeletonCopy);
 //               mesh->SetSkeleton(skeletonCopy);
 //           }
 //       }
 //       meshesByID[mesh->GetMesh()->GetGlobalID()] = mesh->GetMesh();
 //   }
	//for (auto& mesh : object->GetBlendMeshes()) {
 //       m_numDrawsInScene++;
	//	m_numBlendDraws++;
 //       auto meshGlobaId = mesh->GetMesh()->GetGlobalID();
	//	if (meshManager != nullptr) {
 //           if (!meshesByID.contains(meshGlobaId) || meshesByID[meshGlobaId] != mesh->GetMesh()) {
 //               meshManager->AddMesh(mesh->GetMesh(), MaterialBuckets::Blend);
 //           }
 //           meshManager->AddMeshInstance(mesh.get());
 //           if (mesh->GetMesh()->HasBaseSkin()) {
 //               auto skeletonCopy = mesh->GetMesh()->GetBaseSkin()->CopySkeleton();
 //               AddSkeleton(skeletonCopy);
 //               mesh->SetSkeleton(skeletonCopy);
 //           }
 //       }
 //       meshesByID[mesh->GetMesh()->GetGlobalID()] = mesh->GetMesh();
	//}

 //   if (object->HasOpaque()) {
 //       opaqueObjectsByID[object->GetLocalID()] = object;
	//	indirectCommandBufferManager->UpdateBuffersForBucket(MaterialBuckets::Opaque, m_numOpaqueDraws);
 //   }

 //   if (object->HasAlphaTest()) {
 //       alphaTestObjectsByID[object->GetLocalID()] = object;
	//	indirectCommandBufferManager->UpdateBuffersForBucket(MaterialBuckets::AlphaTest, m_numAlphaTestDraws);
 //   }

	//if (object->HasBlend()) {
	//	blendObjectsByID[object->GetLocalID()] = object;
	//	indirectCommandBufferManager->UpdateBuffersForBucket(MaterialBuckets::Blend, m_numBlendDraws);
	//}

	//if (objectManager != nullptr) {
	//	objectManager->AddObject(object);
	//}

	//if (object->HasSkinned()) {
	//	if (object->HasOpaque()) {
	//		opaqueSkinnedObjectsByID[object->GetLocalID()] = object;
	//	} if (object->HasAlphaTest()) {
	//		alphaTestSkinnedObjectsByID[object->GetLocalID()] = object;
	//	} if (object->HasBlend()) {
	//		blendSkinnedObjectsByID[object->GetLocalID()] = object;
	//	}
	//}

	return 0;// object->GetLocalID();
}

UINT Scene::AddNode(std::shared_ptr<SceneNode> node, bool canAttachToRoot) {
    //node->SetLocalID(nextNodeID);

    //if (node->parent == nullptr && canAttachToRoot) {
    //    sceneRoot.AddChild(node);
    //}

    //nodesByID[nextNodeID] = node;
    //nextNodeID++;
    //if (node->m_name != L"") {
    //    nodesByName[node->m_name] = node;
    //}
	return 0;// node->GetLocalID();
}

UINT Scene::AddLight(std::shared_ptr<Light> light) {
    //light->SetLocalID(nextNodeID);
    //if (light->parent == nullptr) {
    //    sceneRoot.AddChild(light);
    //}

    //lightsByID[nextNodeID] = light;
    //nextNodeID++;

    //if (lightManager != nullptr) {
    //    lightManager->AddLight(light.get(), pCamera.get());
    //}
	return 0;// light->GetLocalID();
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
		m_managerInterface.GetLightManager()->AddLight(&lightInfo);
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

std::shared_ptr<SceneNode> Scene::CreateNode(std::wstring name) {
    std::shared_ptr<SceneNode> node = SceneNode::CreateShared(name);
    AddNode(node);
    return node;
}

std::shared_ptr<RenderableObject> Scene::CreateRenderableObject(const std::vector<std::shared_ptr<Mesh>>& meshes, std::wstring name) {
    std::shared_ptr<RenderableObject> object = std::make_shared<RenderableObject>(name, meshes);
    AddObject(object);
    return object;
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
		entity.set<ObjectDrawInfo>(drawInfo);

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

std::shared_ptr<RenderableObject> Scene::GetObjectByName(const std::wstring& name) {
    auto it = objectsByName.find(name);
    if (it != objectsByName.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<RenderableObject> Scene::GetObjectByID(UINT id) {
    auto it = objectsByID.find(id);
    if (it != objectsByID.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<SceneNode> Scene::GetEntityByID(UINT id) {
    auto it = objectsByID.find(id);
    if (it != objectsByID.end()) {
        return it->second;
    }
    auto it1 = lightsByID.find(id);
    if (it1 != lightsByID.end()) {
        return it1->second;
    }
    auto it2 = nodesByID.find(id);
    if (it2 != nodesByID.end()) {
        return it2->second;
    }
    return nullptr;
}

void Scene::RemoveObjectByName(const std::wstring& name) {
    auto it = objectsByName.find(name);
	RemoveObjectByID(it->second->GetLocalID());
}

void Scene::RemoveObjectByID(UINT id) {
  //  auto it = objectsByID.find(id);
  //  if (it != objectsByID.end()) {
  //      auto nameIt = std::find_if(objectsByName.begin(), objectsByName.end(),
  //          [&](const auto& pair) { return pair.second == it->second; });
  //      if (nameIt != objectsByName.end()) {
  //          objectsByName.erase(nameIt);
  //      }
  //      opaqueObjectsByID.erase(it->second->GetLocalID());
  //      alphaTestObjectsByID.erase(it->second->GetLocalID());
		//blendObjectsByID.erase(it->second->GetLocalID());

  //      std::shared_ptr<SceneNode> node = it->second;
  //      node->parent->RemoveChild(node);
		//if (objectManager != nullptr) {
		//	objectManager->RemoveObject(it->second);
		//}
  //      for (auto& mesh : it->second->GetOpaqueMeshes()) {
  //          // TODO: Remove mesh from mesh manager, handling the case where the mesh is used by multiple objects
  //          m_numDrawsInScene--;
		//	m_numOpaqueDraws--;
		//	indirectCommandBufferManager->UpdateBuffersForBucket(MaterialBuckets::Opaque, m_numOpaqueDraws);
  //      }
  //      for (auto& mesh : it->second->GetAlphaTestMeshes()) {
  //          m_numDrawsInScene--;
		//	m_numAlphaTestDraws--;
		//	indirectCommandBufferManager->UpdateBuffersForBucket(MaterialBuckets::AlphaTest, m_numAlphaTestDraws);
  //      }
		//for (auto& mesh : it->second->GetBlendMeshes()) {
		//	m_numDrawsInScene--;
		//	m_numBlendDraws--;
		//	indirectCommandBufferManager->UpdateBuffersForBucket(MaterialBuckets::Blend, m_numBlendDraws);
		//}
		//if (it->second->HasSkinned()) {
		//	if (it->second->HasOpaque()) {
		//		opaqueSkinnedObjectsByID.erase(it->second->GetLocalID());
		//	} if (it->second->HasAlphaTest()) {
		//		alphaTestSkinnedObjectsByID.erase(it->second->GetLocalID());
  //          } if (it->second->HasBlend()) {
  //              blendSkinnedObjectsByID.erase(it->second->GetLocalID());
  //          }
		//}
  //      DeletionManager::GetInstance().MarkForDelete(it->second); // defer deletion to the end of the frame
  //      objectsByID.erase(it);
  //  }
}

void Scene::RemoveLightByID(UINT id) {
    //auto it = lightsByID.find(id);
    //if (it != lightsByID.end()) {
    //    auto& light = it->second;
    //    light->parent->RemoveChild(it->second);
    //    if (lightManager != nullptr) {
    //        lightManager->RemoveLight(light.get());
    //    }
    //    lightsByID.erase(it);
    //}
}

void Scene::RemoveNodeByID(UINT id) {
	auto it = nodesByID.find(id);
	if (it != nodesByID.end()) {
		auto& node = it->second;

		std::vector<std::shared_ptr<SceneNode>> childrenToRemove;
        for (auto& childNode : node->children) {
			childrenToRemove.push_back(childNode);
        }
		for (auto& childNode : childrenToRemove) {
			node->parent->AddChild(childNode);
		}

		node->parent->RemoveChild(it->second);
		nodesByID.erase(it);
	}
}

void Scene::RemoveEntityByID(UINT id, bool recurse) {
	auto it = objectsByID.find(id);
	if (it != objectsByID.end()) {
		if (recurse) {
			auto& object = it->second;
			std::vector<std::shared_ptr<SceneNode>> childrenToRemove;
			for (auto& child : object->children) {
				childrenToRemove.push_back(child);
			}
			for (auto& child : childrenToRemove) {
				RemoveEntityByID(child->GetLocalID(), recurse);
			}
		}
		RemoveObjectByID(id);
	}
	auto it1 = lightsByID.find(id);
	if (it1 != lightsByID.end()) {
		if (recurse) {
			auto& light = it1->second;
			std::vector<std::shared_ptr<SceneNode>> childrenToRemove;
			for (auto& child : light->children) {
				childrenToRemove.push_back(child);
			}
            for (auto& child : childrenToRemove) {
                RemoveEntityByID(child->GetLocalID(), recurse);
            }
		}
		RemoveLightByID(id);
	}
	auto it2 = nodesByID.find(id);
	if (it2 != nodesByID.end()) {
		if (recurse) {
			auto& node = it2->second;
			std::vector<std::shared_ptr<SceneNode>> childrenToRemove;
			for (auto& child : node->children) {
				childrenToRemove.push_back(child);
			}
			for (auto& child : childrenToRemove) {
				RemoveEntityByID(child->GetLocalID(), recurse);
			}
		}
		RemoveNodeByID(id);
	}
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetRenderableObjectIDMap() {
    return objectsByID;
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetOpaqueRenderableObjectIDMap() {
    return opaqueObjectsByID;
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetAlphaTestRenderableObjectIDMap() {
    return alphaTestObjectsByID;
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetBlendRenderableObjectIDMap() {
	return blendObjectsByID;
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetOpaqueSkinnedRenderableObjectIDMap() {
	return opaqueSkinnedObjectsByID;
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetAlphaTestSkinnedRenderableObjectIDMap() {
	return alphaTestSkinnedObjectsByID;
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetBlendSkinnedRenderableObjectIDMap() {
	return blendSkinnedObjectsByID;
}

std::unordered_map<UINT, std::shared_ptr<Light>>& Scene::GetLightIDMap() {
	return lightsByID;
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

 //   if (pCamera != nullptr) {
 //       indirectCommandBufferManager->UnregisterBuffers(GetCamera()->GetLocalID());
 //       m_pPrimaryCameraOpaqueIndirectCommandBuffer = nullptr;
	//	m_pPrimaryCameraAlphaTestIndirectCommandBuffer = nullptr;
	//	m_pPrimaryCameraBlendIndirectCommandBuffer = nullptr;

	//	m_pCameraManager->RemoveCamera(pCamera->GetCameraBufferView());
	//	pCamera->SetCameraBufferView(nullptr);
 //   }

 //   pCamera = std::make_shared<Camera>(L"MainCamera", lookAt, up, fov, aspect, zNear, zFar);
 //   CameraInfo info;
	//pCamera->SetCameraBufferView(m_pCameraManager->AddCamera(info));

 //   setDirectionalLightCascadeSplits(calculateCascadeSplits(getNumDirectionalLightCascades(), zNear, getMaxShadowDistance(), 100.f));
	//if (lightManager != nullptr) {
	//	lightManager->SetCurrentCamera(pCamera.get());
	//}
 //   AddNode(pCamera);
 //   m_pPrimaryCameraOpaqueIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(pCamera->GetLocalID(), MaterialBuckets::Opaque);
	//m_pPrimaryCameraAlphaTestIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(pCamera->GetLocalID(), MaterialBuckets::AlphaTest);
	//m_pPrimaryCameraBlendIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(pCamera->GetLocalID(), MaterialBuckets::Blend);
}

std::shared_ptr<Camera> Scene::GetCamera() {
    return pCamera;
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

UINT Scene::GetNumLights() {
    return lightsByID.size();
}

UINT Scene::GetLightBufferDescriptorIndex() {
    return 0;// lightManager->GetLightBufferDescriptorIndex();
}

UINT Scene::GetPointCubemapMatricesDescriptorIndex() {
    return 0;// lightManager->GetPointCubemapMatricesDescriptorIndex();
}

UINT Scene::GetSpotMatricesDescriptorIndex() {
    return 0;//lightManager->GetSpotMatricesDescriptorIndex();
}

UINT Scene::GetDirectionalCascadeMatricesDescriptorIndex() {
    return 0;//lightManager->GetDirectionalCascadeMatricesDescriptorIndex();
}

std::shared_ptr<SceneNode> Scene::AppendScene(Scene& scene) {
	auto root = scene.GetRoot();
	root.child_of(ECSSceneRoot);
 //   std::unordered_map<UINT, UINT> idMap;
 //   auto oldRootID = scene.sceneRoot.GetLocalID();
 //   auto newRootNode = SceneNode::CreateShared();
 //   for (auto& child : scene.sceneRoot.children) {
 //       auto dummyNode = SceneNode::CreateShared();
	//	dummyNode->SetLocalID(child->GetLocalID());
 //       newRootNode->AddChild(dummyNode);
 //   }
 //   newRootNode->transform = scene.sceneRoot.transform.copy();
	//newRootNode->m_name = scene.sceneRoot.m_name;
 //   UINT newRootID = AddNode(newRootNode);
 //   idMap[oldRootID] = newRootID;

 //   std::vector<std::shared_ptr<SceneNode>> newEntities;

 //   // Parse lights
 //   for (auto& lightPair : scene.lightsByID) {
 //       auto& light = lightPair.second;
 //       UINT oldID = light->GetLocalID();
	//	auto newLight = Light::CopyLight(light->GetLightInfo());
 //       for (auto& child : light->children) {
 //           auto dummyNode = SceneNode::CreateShared();
	//		dummyNode->SetLocalID(child->GetLocalID());
 //           newLight->AddChild(dummyNode);
 //       }
 //       newLight->transform = light->transform.copy();
	//	newLight->m_name = light->m_name;
 //       UINT newID = AddLight(newLight);
 //       idMap[oldID] = newLight->GetLocalID();;
 //       newEntities.push_back(newLight);
 //   }

 //   // Parse objects
 //   for (auto& objectPair : scene.objectsByID) {
 //       auto& object = objectPair.second;
 //       UINT oldID = object->GetLocalID();
 //       auto newObject = std::make_shared<RenderableObject>(object->m_name, object->GetOpaqueMeshes(), object->GetAlphaTestMeshes(), object->GetBlendMeshes());
 //       for (auto& child : object->children) {
 //           auto dummyNode = SceneNode::CreateShared();
 //           dummyNode->SetLocalID(child->GetLocalID());
 //           newObject->AddChild(dummyNode);
 //       }
 //       newObject->transform = object->transform.copy();
 //       newObject->m_name = object->m_name;
 //       UINT newID = AddObject(newObject);
 //       newObject->SetAnimationSpeed(object->GetAnimationSpeed());
 //       idMap[oldID] = newID;
 //       newEntities.push_back(newObject);
 //   }

 //   // Parse nodes
 //   for (auto& nodePair : scene.nodesByID) {
 //       auto& node = nodePair.second;
 //       UINT oldID = node->GetLocalID();
 //       auto newNode = SceneNode::CreateShared();
 //       for (auto& child : node->children) {
 //           auto dummyNode = SceneNode::CreateShared();
 //           dummyNode->SetLocalID(child->GetLocalID());
 //           newNode->AddChild(dummyNode);
 //       }
 //       newNode->transform = node->transform.copy();
	//	newNode->m_name = node->m_name;
 //       UINT newID = AddNode(newNode);
 //       idMap[oldID] = newID;
 //       newEntities.push_back(newNode);
 //   }

 //   // Rebuild parent-child mapping
 //   auto oldRootChildren = newRootNode->children; // Copy existing children
 //   newRootNode->children.clear(); // Clear children

 //   for (auto& child : oldRootChildren) {
 //       if (idMap.find(child->GetLocalID()) != idMap.end()) {
 //           auto mappedChild = GetEntityByID(idMap[child->GetLocalID()]);
 //           if (mappedChild) {
 //               newRootNode->AddChild(mappedChild);
 //           }
 //       }
 //   }

 //   for (auto& entity : newEntities) {
 //       auto oldChildren = entity->children; // Copy existing children
 //       entity->children.clear(); // Clear children

 //       for (auto& child : oldChildren) {
 //           if (idMap.find(child->GetLocalID()) != idMap.end()) {
 //               auto mappedChild = GetEntityByID(idMap[child->GetLocalID()]);
 //               if (mappedChild) {
 //                   entity->AddChild(mappedChild);
 //               }
 //               else {
 //                   spdlog::error("Node missing from id map: ID {}", child->GetLocalID());
 //               }
 //           }
 //           else {
 //               spdlog::error("Node missing from id map: ID {}", child->GetLocalID());
 //           }
 //       }
 //   }
 //   return newRootNode;
      return nullptr;
}

void Scene::MakeResident() {
	//meshManager = MeshManager::CreateUnique();
	//for (auto& objectPair : objectsByID) {
	//	auto& object = objectPair.second;
	//	for (auto& mesh : object->GetOpaqueMeshes()) {
 //           auto meshGlobaId = mesh->GetMesh()->GetGlobalID();
 //           if (!meshesByID.contains(meshGlobaId) || meshesByID[meshGlobaId] != mesh->GetMesh()) {
 //               meshManager->AddMesh(mesh->GetMesh(), MaterialBuckets::Opaque);
 //           }
	//		meshManager->AddMeshInstance(mesh.get());
	//	}
	//	for (auto& mesh : object->GetAlphaTestMeshes()) {
 //           auto meshGlobaId = mesh->GetMesh()->GetGlobalID();
 //           if (!meshesByID.contains(meshGlobaId) || meshesByID[meshGlobaId] != mesh->GetMesh()) {
 //               meshManager->AddMesh(mesh->GetMesh(), MaterialBuckets::AlphaTest);
 //           }
 //           meshManager->AddMeshInstance(mesh.get());
	//	}
	//	for (auto& mesh : object->GetBlendMeshes()) {
 //           auto meshGlobaId = mesh->GetMesh()->GetGlobalID();
 //           if (!meshesByID.contains(meshGlobaId) || meshesByID[meshGlobaId] != mesh->GetMesh()) {
 //               meshManager->AddMesh(mesh->GetMesh(), MaterialBuckets::Blend);
 //           }
 //           meshManager->AddMeshInstance(mesh.get());
	//	}
	//	if (object->HasOpaque()) {
	//		opaqueObjectsByID[object->GetLocalID()] = object;
	//		indirectCommandBufferManager->UpdateBuffersForBucket(MaterialBuckets::Opaque, m_numOpaqueDraws);
	//	}
	//}
	//objectManager = ObjectManager::CreateUnique();
	//for (auto& objectPair : objectsByID) {
	//	auto& object = objectPair.second;
	//	objectManager->AddObject(object);
	//}
	//if (GetCamera() != nullptr) {
 //       m_pPrimaryCameraOpaqueIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(GetCamera()->GetLocalID(), MaterialBuckets::Opaque);
	//	m_pPrimaryCameraAlphaTestIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(GetCamera()->GetLocalID(), MaterialBuckets::AlphaTest);
	//	m_pPrimaryCameraBlendIndirectCommandBuffer = indirectCommandBufferManager->CreateBuffer(GetCamera()->GetLocalID(), MaterialBuckets::Blend);
 //   }

 //   lightManager = LightManager::CreateUnique();

	//m_pCameraManager = CameraManager::CreateUnique();

	//lightManager->SetCameraManager(m_pCameraManager.get());
	//if (pCamera != nullptr) {
	//	lightManager->SetCurrentCamera(pCamera.get());
	//}
 //   lightManager->SetCommandBufferManager(indirectCommandBufferManager.get());
}

void Scene::MakeNonResident() {

    /*auto& debugPtrManager = DebugSharedPtrManager::GetInstance();

    meshManager = nullptr;
	objectManager = nullptr;

    if (GetCamera() != nullptr) {
        indirectCommandBufferManager->UnregisterBuffers(GetCamera()->GetLocalID());
        m_pPrimaryCameraOpaqueIndirectCommandBuffer = nullptr;
		m_pPrimaryCameraAlphaTestIndirectCommandBuffer = nullptr;
		m_pPrimaryCameraBlendIndirectCommandBuffer = nullptr;
    }

	m_pCameraManager = nullptr;
	lightManager = nullptr;*/
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