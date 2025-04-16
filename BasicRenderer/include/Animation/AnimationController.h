#pragma once

#include "Animation/AnimationClip.h"
#include "Components.h"

class AnimationController {
public:
    std::shared_ptr<AnimationClip> animationClip;
    float currentTime = 0;
    bool isPlaying;

    AnimationController();
    AnimationController(const AnimationController& other);

    void setAnimationClip(std::shared_ptr<AnimationClip> animationClip);
    void reset();
    void pause();
    void unpause();
    Components::Transform& GetUpdatedTransform(float elapsedTime, bool force = false);
    void SetAnimationSpeed(float speed) { m_animationSpeed = speed; }
    float GetAnimationSpeed() { return m_animationSpeed; }
	unsigned int m_lastPositionKeyframeIndex = 0;
	unsigned int m_lastRotationKeyframeIndex = 0;
	unsigned int m_lastScaleKeyframeIndex = 0;

private:
	Components::Transform m_transform;
    float m_animationSpeed = 1.0f;
    void UpdateTransform();
};