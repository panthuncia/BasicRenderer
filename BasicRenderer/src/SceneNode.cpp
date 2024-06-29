#include "SceneNode.h"
#include "AnimationController.h"

SceneNode::SceneNode(const std::string& name)
    : parent(nullptr), transform(), animationController(std::make_unique<AnimationController>(this)), localID(-1), name(name) {
}

void SceneNode::addChild(std::shared_ptr<SceneNode> node) {
    children[node->localID] = node;
    if (node->parent != nullptr) {
        node->parent->removeChild(node->localID);
    }
    node->parent = this;
}

void SceneNode::removeChild(unsigned int childId) {
    children.erase(childId);
}

void SceneNode::update() {
    if (transform.isDirty) {
        forceUpdate();
    }
    onUpdate();
    for (auto& childPair : children) {
        childPair.second->update();
    }
}

void SceneNode::forceUpdate() {
    if (parent) {
        transform.computeModelMatrixFromParent(parent->transform.modelMatrix);
    }
    else {
        transform.computeLocalModelMatrix();
    }
    onUpdate();
    for (auto& childPair : children) {
        childPair.second->forceUpdate();
    }
}
