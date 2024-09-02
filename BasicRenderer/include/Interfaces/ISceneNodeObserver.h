#pragma once

template <typename T>
class ISceneNodeObserver {
public:
    virtual ~ISceneNodeObserver() = default;
    virtual void OnNodeUpdated(T * node) = 0; // Templated method for specific node type
};