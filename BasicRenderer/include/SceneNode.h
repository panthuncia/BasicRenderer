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
	static std::shared_ptr<SceneNode> CreateShared(std::wstring name = L"") {
		return std::shared_ptr<SceneNode>(new SceneNode(name));
	}
    std::unordered_map<unsigned int, std::shared_ptr<SceneNode>> children;
    SceneNode* parent = nullptr;
    Transform transform;
    std::unique_ptr<AnimationController> animationController; // Use unique_ptr to avoid incomplete type
    std::wstring m_name;

    void AddChild(std::shared_ptr<SceneNode> node);
    void AddChild(SceneNode* node);
    void RemoveChild(unsigned int childId);
    void Update();
    void ForceUpdate();
	void SetLocalID(int id) { localID = id; }
	int GetLocalID() { return localID; }

    void AddObserver(ISceneNodeObserver<SceneNode>* observer);
    void RemoveObserver(ISceneNodeObserver<SceneNode>* observer);

private:
    std::vector<ISceneNodeObserver<SceneNode>*> observers; // Base class observer
protected:
    SceneNode(const std::wstring& name = L"");
    virtual void OnUpdate() {} // Hook method for derived classes to extend update behavior
    void NotifyObservers();
    int localID;

    friend class Scene;
};