#include "SceneNode.h"
#include "AnimationController.h"

SceneNode::SceneNode(const std::wstring& name)
    : parent(nullptr), transform(), animationController(std::make_unique<AnimationController>(this)), localID(-1), m_name(name) {
}

void SceneNode::AddChild(std::shared_ptr<SceneNode> node) {
    children.insert(node);
    if (node->parent != nullptr) {
        node->parent->RemoveChild(node);
    }
    node->parent = this;
}

void SceneNode::RemoveChild(std::shared_ptr<SceneNode> child) {
    children.erase(child);
}

void SceneNode::Update() {
    if (transform.isDirty) {
        ForceUpdate();
    }
    for (auto& child : children) {
        child->Update();
    }
}

void SceneNode::ForceUpdate() {
    if (parent) {
        transform.computeModelMatrixFromParent(parent->transform.modelMatrix);
    }
    else {
        transform.computeLocalModelMatrix();
    }
    OnUpdate();
    for (auto& child : children) {
        child->ForceUpdate();
    }
    NotifyObservers();
}

void SceneNode::AddObserver(ISceneNodeObserver<SceneNode>* observer) {
    if (observer && std::find(observers.begin(), observers.end(), observer) == observers.end()) {
        observers.push_back(observer);
    }
}

void SceneNode::RemoveObserver(ISceneNodeObserver<SceneNode>* observer) {
    auto it = std::remove(observers.begin(), observers.end(), observer);
    if (it != observers.end()) {
        observers.erase(it);
    }
}

void SceneNode::NotifyObservers() {
    for (auto observer : observers) {
        observer->OnNodeUpdated(this);
    }
}