#include "AnimationController.h"
#include "Transform.h"
#include "SceneNode.h"

AnimationController::AnimationController()
    : animationClip(nullptr), currentTime(0.0f), isPlaying(true) {
}

AnimationController::AnimationController(const AnimationController& other)
	: animationClip(other.animationClip), currentTime(other.currentTime), isPlaying(other.isPlaying) {
}

void AnimationController::setAnimationClip(std::shared_ptr<AnimationClip> animationClip) {
    this->animationClip = animationClip;
    //node->ForceUpdate();
    UpdateTransform();
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

Transform& AnimationController::GetUpdatedTransform(float elapsedTime, bool force) {
    if (!force && (!isPlaying || !animationClip)) return;

    currentTime += elapsedTime * m_animationSpeed;
    currentTime = fmod(currentTime, animationClip->duration);

    UpdateTransform();
    return m_transform;
}

std::pair<unsigned int, unsigned int> findBoundingKeyframes(float currentTime, std::vector<Keyframe>& keyframes, unsigned int& counter) {
	unsigned int prevKeyframeIndex = 0;
	unsigned int nextKeyframeIndex = 0;

	if (keyframes.size() == 1) {
		return std::make_pair(prevKeyframeIndex, nextKeyframeIndex);
	}

    bool found = false;
    for (size_t i = counter; i < keyframes.size() - 1; ++i) {
        if (currentTime >= keyframes[i].time && currentTime < keyframes[i + 1].time) {
            prevKeyframeIndex = i;
            nextKeyframeIndex = i + 1;
            counter = i;
            found = true;
            break;
        }
    }
	if (!found) { // We've wrapped around
        counter = 0;
        return findBoundingKeyframes(currentTime, keyframes, counter);
    }

    return std::make_pair(prevKeyframeIndex, nextKeyframeIndex);
    };

Transform& AnimationController::UpdateTransform() {
    if (!animationClip) return;

    auto lerpVec3 = [](const XMVECTOR& start, const XMVECTOR& end, float t) {
        XMVECTOR lerped = XMVectorLerp(start, end, t);
        return lerped;
        };

    auto lerpRotation = [](const XMVECTOR& start, const XMVECTOR& end, float t) {
        XMVECTOR lerped = XMQuaternionSlerp(start, end, t);
        return lerped;
        };

    // Check if position keyframes are available
    if (!animationClip->positionKeyframes.empty()) {
        auto boundingPositionFrames = findBoundingKeyframes(currentTime, animationClip->positionKeyframes, m_lastPositionKeyframeIndex);
		Keyframe* prevKeyframe = &animationClip->positionKeyframes[boundingPositionFrames.first];
		Keyframe* nextKeyframe = &animationClip->positionKeyframes[boundingPositionFrames.second];
        if (prevKeyframe->time != nextKeyframe->time) {
            float timeElapsed = currentTime - prevKeyframe->time;
            float diff = nextKeyframe->time - prevKeyframe->time;
            float t = diff > 0 ? timeElapsed / diff : 0;
            XMVECTOR interpolatedPosition = lerpVec3(prevKeyframe->value, nextKeyframe->value, t);
            //node->transform.setLocalPosition(interpolatedPosition);
        }
    }

    // Check if rotation keyframes are available
    if (!animationClip->rotationKeyframes.empty()) {
        auto boundingRotationFrames = findBoundingKeyframes(currentTime, animationClip->rotationKeyframes, m_lastRotationKeyframeIndex);
		Keyframe* prevKeyframe = &animationClip->rotationKeyframes[boundingRotationFrames.first];
		Keyframe* nextKeyframe = &animationClip->rotationKeyframes[boundingRotationFrames.second];
        if (prevKeyframe->time != nextKeyframe->time) {
            float timeElapsed = currentTime - prevKeyframe->time;
            float diff = nextKeyframe->time - prevKeyframe->time;
            float t = diff > 0 ? timeElapsed / diff : 0;
            XMVECTOR interpolatedRotation = lerpRotation(prevKeyframe->value, nextKeyframe->value, t);
            //node->transform.setLocalRotationFromQuaternion(interpolatedRotation);
        }
    }

    // Check if scale keyframes are available
    if (!animationClip->scaleKeyframes.empty()) {
        auto boundingScaleFrames = findBoundingKeyframes(currentTime, animationClip->scaleKeyframes, m_lastScaleKeyframeIndex);
		Keyframe* prevKeyframe = &animationClip->scaleKeyframes[boundingScaleFrames.first];
		Keyframe* nextKeyframe = &animationClip->scaleKeyframes[boundingScaleFrames.second];
        if (prevKeyframe->time != nextKeyframe->time) {
            float timeElapsed = currentTime - prevKeyframe->time;
            float diff = nextKeyframe->time - prevKeyframe->time;
            float t = diff > 0 ? timeElapsed / diff : 0;
            XMVECTOR interpolatedScale = lerpVec3(prevKeyframe->value, nextKeyframe->value, t);
            //node->transform.setLocalScale(interpolatedScale);
        }
    }
}
