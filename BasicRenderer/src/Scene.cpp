#include "Scene.h"

UINT Scene::addObject(std::shared_ptr<RenderableObject> object) {
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

std::shared_ptr<RenderableObject> Scene::getObjectByName(const std::string& name) {
    auto it = objectsByName.find(name);
    if (it != objectsByName.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<RenderableObject> Scene::getObjectByID(UINT id) {
    auto it = objectsByID.find(id);
    if (it != objectsByID.end()) {
        return it->second;
    }
    return nullptr;
}

void Scene::removeObjectByName(const std::string& name) {
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

void Scene::removeObjectByID(UINT id) {
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

std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& Scene::getRenderableObjectIDMap() {
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