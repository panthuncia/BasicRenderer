#include "Scene.h"

#include "Utilities.h"

UINT Scene::AddObject(std::shared_ptr<RenderableObject> object) {
    numObjects++;
    object->localID = nextNodeID;
    objectsByName[object->name] = object;
    objectsByID[nextNodeID] = object;
    nextNodeID++;

    if (object->parent == nullptr) {
        sceneRoot.addChild(object);
    }

    return object->localID;
}

UINT Scene::AddNode(std::shared_ptr<SceneNode> node) {
    node->localID = nextNodeID;

    if (node->parent == nullptr) {
        sceneRoot.addChild(node);
    }

    nodesByID[nextNodeID] = node;
    nextNodeID++;
    if (node->name != "") {
        nodesByName[node->name] = node;
    }
    return node->localID;
}

std::shared_ptr<SceneNode> Scene::CreateNode(std::string name) {
    std::shared_ptr<SceneNode> node = std::make_shared<SceneNode>(name);
    AddNode(node);
    return node;
}

std::shared_ptr<RenderableObject> Scene::CreateRenderableObject(MeshData meshData, std::string name) {
    std::shared_ptr<RenderableObject> object = RenderableFromData(meshData, name);
    AddObject(object);
    return object;
}


std::shared_ptr<RenderableObject> Scene::GetObjectByName(const std::string& name) {
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

void Scene::RemoveObjectByName(const std::string& name) {
    auto it = objectsByName.find(name);
    if (it != objectsByName.end()) {
        auto idIt = std::find_if(objectsByID.begin(), objectsByID.end(),
            [&](const auto& pair) { return pair.second == it->second; });
        if (idIt != objectsByID.end()) {
            objectsByID.erase(idIt);
        }
        objectsByName.erase(it);
        std::shared_ptr<SceneNode> node = it->second;
        node->parent->removeChild(node->localID);
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
        std::shared_ptr<SceneNode> node = it->second;
        node->parent->removeChild(node->localID);
    }
}

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::GetRenderableObjectIDMap() {
    return objectsByID;
}

SceneNode& Scene::GetRoot() {
    return sceneRoot;
}

void Scene::Update() {
    auto currentTime = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = currentTime - lastUpdateTime;
    lastUpdateTime = currentTime;
    this->sceneRoot.update();
}

void Scene::SetCamera(XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) {
    pCamera = std::make_shared<Camera>("MainCamera", lookAt, up, fov, aspect, zNear, zFar);
    AddNode(pCamera);
}

std::shared_ptr<Camera> Scene::GetCamera() {
    return pCamera;
}