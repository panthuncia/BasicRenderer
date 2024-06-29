#include "AnimationController.h"
#include "Transform.h"
#include "SceneNode.h"

AnimationController::AnimationController(SceneNode* node)
    : node(node), animationClip(nullptr), currentTime(0.0f), isPlaying(true) {
}

void AnimationController::setAnimationClip(std::shared_ptr<AnimationClip> animationClip) {
    this->animationClip = animationClip;
    node->forceUpdate();
    updateTransform();
}

void AnimationController::reset() {
    currentTime = 0.0f;
}

void AnimationController::pause() {
    isPlaying = false;
}

void AnimationController::unpause() {
    isPlaying = true;
}

void AnimationController::update(float elapsedTime, bool force) {
    if (!force && (!isPlaying || !animationClip)) return;

    currentTime += elapsedTime;
    currentTime = fmod(currentTime, animationClip->duration);

    updateTransform();
}

void AnimationController::updateTransform() {
    if (!animationClip) return;

    auto findBoundingKeyframes = [this](float currentTime, const std::vector<Keyframe>& keyframes) {
        Keyframe prevKeyframe = keyframes.front();
        Keyframe nextKeyframe = keyframes.back();

        for (size_t i = 0; i < keyframes.size() - 1; ++i) {
            if (currentTime >= keyframes[i].time && currentTime < keyframes[i + 1].time) {
                prevKeyframe = keyframes[i];
                nextKeyframe = keyframes[i + 1];
                break;
            }
        }

        return std::make_pair(prevKeyframe, nextKeyframe);
        };

    auto lerpVec3 = [](const XMFLOAT3& start, const XMFLOAT3& end, float t) {
        XMVECTOR s = XMLoadFloat3(&start);
        XMVECTOR e = XMLoadFloat3(&end);
        XMVECTOR lerped = XMVectorLerp(s, e, t);
        XMFLOAT3 result;
        XMStoreFloat3(&result, lerped);
        return result;
        };

    auto lerpRotation = [](const XMVECTOR& start, const XMVECTOR& end, float t) {
        XMVECTOR lerped = XMQuaternionSlerp(start, end, t);
        return lerped;
        };

    // Check if position keyframes are available
    if (!animationClip->positionKeyframes.empty()) {
        auto boundingPositionFrames = findBoundingKeyframes(currentTime, animationClip->positionKeyframes);
        if (boundingPositionFrames.first.time != boundingPositionFrames.second.time) {
            float timeElapsed = currentTime - boundingPositionFrames.first.time;
            float diff = boundingPositionFrames.second.time - boundingPositionFrames.first.time;
            float t = diff > 0 ? timeElapsed / diff : 0;
            XMFLOAT3 interpolatedPosition = lerpVec3(boundingPositionFrames.first.value, boundingPositionFrames.second.value, t);
            node->transform.setLocalPosition(interpolatedPosition);
        }
    }

    // Check if rotation keyframes are available
    if (!animationClip->rotationKeyframes.empty()) {
        auto boundingRotationFrames = findBoundingKeyframes(currentTime, animationClip->rotationKeyframes);
        if (boundingRotationFrames.first.time != boundingRotationFrames.second.time) {
            float timeElapsed = currentTime - boundingRotationFrames.first.time;
            float diff = boundingRotationFrames.second.time - boundingRotationFrames.first.time;
            float t = diff > 0 ? timeElapsed / diff : 0;
            XMVECTOR interpolatedRotation = lerpRotation(boundingRotationFrames.first.rotation, boundingRotationFrames.second.rotation, t);
            node->transform.setLocalRotationFromQuaternion(interpolatedRotation);
        }
    }

    // Check if scale keyframes are available
    if (!animationClip->scaleKeyframes.empty()) {
        auto boundingScaleFrames = findBoundingKeyframes(currentTime, animationClip->scaleKeyframes);
        if (boundingScaleFrames.first.time != boundingScaleFrames.second.time) {
            float timeElapsed = currentTime - boundingScaleFrames.first.time;
            float diff = boundingScaleFrames.second.time - boundingScaleFrames.first.time;
            float t = diff > 0 ? timeElapsed / diff : 0;
            XMFLOAT3 interpolatedScale = lerpVec3(boundingScaleFrames.first.value, boundingScaleFrames.second.value, t);
            node->transform.setLocalScale(interpolatedScale);
        }
    }
}
