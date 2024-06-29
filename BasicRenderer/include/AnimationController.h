#pragma once

#include "SceneNode.h"
#include "AnimationClip.h"

class SceneNode;

class AnimationController {
public:
    SceneNode* node;
    std::shared_ptr<AnimationClip> animationClip;
    float currentTime;
    bool isPlaying;

    AnimationController(SceneNode* node);

    void setAnimationClip(std::shared_ptr<AnimationClip> animationClip);
    void reset();
    void pause();
    void unpause();
    void update(float elapsedTime, bool force = false);

private:
    void updateTransform();
};