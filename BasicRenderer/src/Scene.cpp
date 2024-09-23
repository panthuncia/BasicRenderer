#include "Scene.h"

#include "Utilities.h"
#include "SettingsManager.h"

Scene::Scene(){
    getNumDirectionalLightCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades");
    getMaxShadowDistance = SettingsManager::GetInstance().getSettingGetter<float>("maxShadowDistance");
    setDirectionalLightCascadeSplits = SettingsManager::GetInstance().getSettingSetter<std::vector<float>>("directionalLightCascadeSplits");
}

UINT Scene::AddObject(std::shared_ptr<RenderableObject> object) {
    numObjects++;
    object->localID = nextNodeID;
    objectsByName[object->m_name] = object;
    objectsByID[nextNodeID] = object;
    nextNodeID++;

    if (object->HasOpaque()) {
        opaqueObjectsByID[object->localID] = object;
    }

    if (object->HasTransparent()) {
        transparentObjectsByID[object->localID] = object;
    }

    if (object->parent == nullptr) {
        sceneRoot.AddChild(object);
    }

    return object->localID;
}

UINT Scene::AddNode(std::shared_ptr<SceneNode> node) {
    node->localID = nextNodeID;

    if (node->parent == nullptr) {
        sceneRoot.AddChild(node);
    }

    nodesByID[nextNodeID] = node;
    nextNodeID++;
    if (node->m_name != L"") {
        nodesByName[node->m_name] = node;
    }
    return node->localID;
}

UINT Scene::AddLight(std::shared_ptr<Light> light, bool shadowCasting) {
    light->localID = nextNodeID;
    if (light->parent == nullptr) {
        sceneRoot.AddChild(light);
    }

    lightsByID[nextNodeID] = light;
    nextNodeID++;

    lightManager.AddLight(light.get(), shadowCasting, pCamera.get());
    return light->localID;
}

std::shared_ptr<SceneNode> Scene::CreateNode(std::wstring name) {
    std::shared_ptr<SceneNode> node = std::make_shared<SceneNode>(name);
    AddNode(node);
    return node;
}

std::shared_ptr<RenderableObject> Scene::CreateRenderableObject(MeshData meshData, std::wstring name) {
    std::shared_ptr<RenderableObject> object = RenderableFromData(meshData, name);
    AddObject(object);
    return object;
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
    if (it != objectsByName.end()) {
        auto idIt = std::find_if(objectsByID.begin(), objectsByID.end(),
            [&](const auto& pair) { return pair.second == it->second; });
        if (idIt != objectsByID.end()) {
            objectsByID.erase(idIt);
        }
        objectsByName.erase(it);
        opaqueObjectsByID.erase(it->second->localID);
        transparentObjectsByID.erase(it->second->localID);
        std::shared_ptr<SceneNode> node = it->second;
        node->parent->RemoveChild(node->localID);
    }
}

void Scene::RemoveObjectByID(UINT id) {
    auto it = objectsByID.find(id);
    if (it != objectsByID.end()) {
        auto nameIt = std::find_if(objectsByName.begin(), objectsByName.end(),
            [&](const auto& pair) { return pair.second == it->second; });
        if (nameIt != objectsByName.end()) {
            objectsByName.erase(nameIt);
        }
        objectsByID.erase(it);
        opaqueObjectsByID.erase(it->second->localID);
        transparentObjectsByID.erase(it->second->localID);

        std::shared_ptr<SceneNode> node = it->second;
        node->parent->RemoveChild(node->localID);
    }
}



void Scene::RemoveLightByID(UINT id) {
    auto it = lightsByID.find(id);
    if (it != lightsByID.end()) {
        auto& light = it->second;
        light->parent->RemoveChild(id);
        lightsByID.erase(it);
        lightManager.RemoveLight(light.get());
    }
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetRenderableObjectIDMap() {
    return objectsByID;
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetOpaqueRenderableObjectIDMap() {
    return opaqueObjectsByID;
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetTransparentRenderableObjectIDMap() {
    return transparentObjectsByID;
}

std::unordered_map<UINT, std::shared_ptr<Light>>& Scene::GetLightIDMap() {
	return lightsByID;
}

SceneNode& Scene::GetRoot() {
    return sceneRoot;
}

void Scene::Update() {
    auto currentTime = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = currentTime - lastUpdateTime;
    lastUpdateTime = currentTime;
    for (auto& skeleton : animatedSkeletons) {
        for (auto& node : skeleton->m_nodes) {
            node->animationController->update(elapsed_seconds.count());
        }
    }
    this->sceneRoot.Update();
    for (auto& skeleton : animatedSkeletons) {
        skeleton->UpdateTransforms();
    }
    PostUpdate();
}

void Scene::SetCamera(XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) {
    pCamera = std::make_shared<Camera>(L"MainCamera", lookAt, up, fov, aspect, zNear, zFar);
    setDirectionalLightCascadeSplits(calculateCascadeSplits(getNumDirectionalLightCascades(), zNear, getMaxShadowDistance(), 100.f));
    lightManager.SetCurrentCamera(pCamera.get());
    AddNode(pCamera);
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
}

void Scene::PostUpdate() {
    lightManager.UpdateBuffers();
}

UINT Scene::GetNumLights() {
    return lightsByID.size();
}

UINT Scene::GetLightBufferDescriptorIndex() {
    return lightManager.GetLightBufferDescriptorIndex();
}

UINT Scene::GetPointCubemapMatricesDescriptorIndex() {
	return lightManager.GetPointCubemapMatricesDescriptorIndex();
}

UINT Scene::GetSpotMatricesDescriptorIndex() {
	return lightManager.GetSpotMatricesDescriptorIndex();
}

UINT Scene::GetDirectionalCascadeMatricesDescriptorIndex() {
	return lightManager.GetDirectionalCascadeMatricesDescriptorIndex();
}

std::shared_ptr<SceneNode> Scene::AppendScene(Scene& scene) {
    std::unordered_map<UINT, UINT> idMap;
    auto& oldRootID = scene.sceneRoot.localID;
    auto newRootNode = std::make_shared<SceneNode>();
    for (auto& childPair : scene.sceneRoot.children) {
        auto& child = childPair.second;
        auto dummyNode = std::make_shared<SceneNode>();
        dummyNode->localID = child->localID;
        newRootNode->AddChild(dummyNode);
    }
    newRootNode->transform = scene.sceneRoot.transform.copy();
    UINT newRootID = AddNode(newRootNode);
    idMap[oldRootID] = newRootID;

    std::vector<std::shared_ptr<SceneNode>> newEntities;

    // Parse lights
    for (auto& lightPair : scene.lightsByID) {
        auto& light = lightPair.second;
        UINT oldID = light->localID;
		auto newLight = Light::CopyLight(light->GetLightInfo());
        for (auto& childPair : light->children) {
            auto& child = childPair.second;
            auto dummyNode = std::make_shared<SceneNode>();
            dummyNode->localID = child->localID;
            newLight->AddChild(dummyNode);
        }
        newLight->transform = light->transform.copy();
        UINT newID = AddLight(newLight);
        idMap[oldID] = newLight->localID;
        newEntities.push_back(newLight);
    }

    // Parse objects
    for (auto& objectPair : scene.objectsByID) {
        auto& object = objectPair.second;
        UINT oldID = object->localID;
        auto newObject = std::make_shared<RenderableObject>(object->m_name, object->GetOpaqueMeshes(), object->GetTransparentMeshes());
        for (auto& childPair : object->children) {
            auto& child = childPair.second;
            auto dummyNode = std::make_shared<SceneNode>();
            dummyNode->localID = child->localID;
            newObject->AddChild(dummyNode);
        }
        newObject->transform = object->transform.copy();
        UINT newID = AddObject(newObject);
        idMap[oldID] = newID;
        newEntities.push_back(newObject);
    }

    // Parse nodes
    for (auto& nodePair : scene.nodesByID) {
        auto& node = nodePair.second;
        UINT oldID = node->localID;
        auto newNode = std::make_shared<SceneNode>();
        for (auto& childPair : node->children) {
            auto& child = childPair.second;
            auto dummyNode = std::make_shared<SceneNode>();
            dummyNode->localID = child->localID;
            newNode->AddChild(dummyNode);
        }
        newNode->transform = node->transform.copy();
        UINT newID = AddNode(newNode);
        idMap[oldID] = newID;
        newEntities.push_back(newNode);
    }

    for (auto& skeleton : scene.skeletons) {
        std::vector<std::shared_ptr<SceneNode>> newJoints;
        for (auto& joint : skeleton->m_nodes) {
            auto newJoint = GetEntityByID(idMap[joint->localID]);
            if (newJoint) {
                newJoints.push_back(newJoint);
            }
            else {
                spdlog::error("Joint mapping broke during scene cloning!");
            }
        }
        //auto newSkeleton = std::make_shared<Skeleton>(newJoints, skeleton->GetInverseBindMatricesHandle());
        auto newSkeleton = std::make_shared<Skeleton>(newJoints, skeleton->m_inverseBindMatrices);
        // Remap node ids in animations
        for (auto& animation : skeleton->animations) {
            auto newAnimation = std::make_shared<Animation>(animation->name);
            for (auto& nodePair : animation->nodesMap) {
                UINT key = nodePair.first;
                newAnimation->nodesMap[idMap[key]] = nodePair.second;
            }
            newSkeleton->AddAnimation(newAnimation);
        }

        // Remap skeleton & users to their correct IDs
        for (auto& oldID : skeleton->userIDs) {
            GetObjectByID(idMap[oldID])->SetSkin(newSkeleton);
        }
        AddSkeleton(newSkeleton);
    }

    // Rebuild parent-child mapping
    auto oldRootChildren = newRootNode->children; // Copy existing children
    newRootNode->children.clear(); // Clear children

    for (auto& childPair : oldRootChildren) {
        auto& child = childPair.second;
        if (idMap.find(child->localID) != idMap.end()) {
            auto mappedChild = GetEntityByID(idMap[child->localID]);
            if (mappedChild) {
                newRootNode->AddChild(mappedChild);
            }
        }
    }

    for (auto& entity : newEntities) {
        auto oldChildren = entity->children; // Copy existing children
        entity->children.clear(); // Clear children

        for (auto& childPair : oldChildren) {
            auto& child = childPair.second;
            if (idMap.find(child->localID) != idMap.end()) {
                auto mappedChild = GetEntityByID(idMap[child->localID]);
                if (mappedChild) {
                    entity->AddChild(mappedChild);
                }
                else {
                    spdlog::error("Node missing from id map: ID {}", child->localID);
                }
            }
            else {
                spdlog::error("Node missing from id map: ID {}", child->localID);
            }
        }
    }
    return newRootNode;
}