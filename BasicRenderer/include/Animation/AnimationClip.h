#pragma once
#include <vector>
#include "Animation/Keyframe.h"

class AnimationClip {
public:
    std::vector<Keyframe> positionKeyframes;
    std::vector<Keyframe> rotationKeyframes;
    std::vector<Keyframe> scaleKeyframes;
    float duration;
    AnimationClip();

    void addPositionKeyframe(float time, const XMFLOAT3& position);
    void addRotationKeyframe(float time, const XMVECTOR& rotation);
    void addScaleKeyframe(float time, const XMFLOAT3& scale);

    std::pair<Keyframe, Keyframe> findBoundingKeyframes(float currentTime, const std::vector<Keyframe>& keyframes) const;

private:
    void updateDuration(float time);
};
