#pragma once

#include "SceneNode.h"
#include "AnimationClip.h"

class SceneNode;

class AnimationController {
public:
    SceneNode* node;
    AnimationClip* animationClip;
    float currentTime;
    bool isPlaying;

    AnimationController(SceneNode* node);

    void setAnimationClip(AnimationClip* animationClip);
    void reset();
    void pause();
    void unpause();
    void update(float elapsedTime, bool force = false);

private:
    void updateTransform();
};