#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "Transform.h"
#include "AnimationController.h"
#include "Interfaces/ISceneNodeObserver.h"

class AnimationController;

class SceneNode {
public:
    std::unordered_map<unsigned int, std::shared_ptr<SceneNode>> children;
    SceneNode* parent = nullptr;
    Transform transform;
    std::unique_ptr<AnimationController> animationController; // Use unique_ptr to avoid incomplete type
    int localID;
    std::wstring m_name;

    SceneNode(const std::wstring& name = L"");

    void AddChild(std::shared_ptr<SceneNode> node);
    void RemoveChild(unsigned int childId);
    void Update();
    void ForceUpdate();

    void AddObserver(ISceneNodeObserver<SceneNode>* observer);
    void RemoveObserver(ISceneNodeObserver<SceneNode>* observer);

private:
    std::vector<ISceneNodeObserver<SceneNode>*> observers; // Base class observer
protected:
    virtual void OnUpdate() {} // Hook method for derived classes to extend update behavior
    void NotifyObservers();
};