#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Transform.h"
#include "AnimationController.h"

class AnimationController;

class SceneNode {
public:
    std::unordered_map<unsigned int, std::shared_ptr<SceneNode>> children;
    SceneNode* parent;
    Transform transform;
    std::unique_ptr<AnimationController> animationController; // Use unique_ptr to avoid incomplete type issue
    int localID;
    std::string name;

    SceneNode(const std::string& name = "");

    void addChild(std::shared_ptr<SceneNode> node);
    void removeChild(unsigned int childId);
    void update();
    void forceUpdate();
};