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
    void SetAnimationSpeed(float speed) { m_animationSpeed = speed; }
    float GetAnimationSpeed() { return m_animationSpeed; }

private:
    float m_animationSpeed;
    void updateTransform();
};