#include "anim/hobi/Transition.h"

#include <cmath>

#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/ClipState.h"
#include "anim/hobi/RetargetClipState.h"

namespace anim {

HobiState* ClipTransition::createState(HobiPlayable& root) {
    if (clip == nullptr) return nullptr;
    return new ClipState(&root, *clip);
}

void ClipTransition::apply(HobiState& state) {
    state.setSpeed(speedValue);
    if (!std::isnan(normalizedStartTime)) state.setNormalizedTime(normalizedStartTime);
    state.setEvents(&eventSequence);
}

HobiState* RetargetClipTransition::createState(HobiPlayable& root) {
    if (!isValid()) return nullptr;
    return new RetargetClipState(&root, *clip, *sourceSkeleton, *sourceAvatar,
                                 *targetAvatar);
}

void RetargetClipTransition::apply(HobiState& state) {
    state.setSpeed(speedValue);
    if (!std::isnan(normalizedStartTime)) state.setNormalizedTime(normalizedStartTime);
    state.setEvents(&eventSequence);
}

float ClipTransition::length() const {
    if (clip == nullptr) return 0.0f;

    float normalizedEndTime = eventSequence.normalizedEndTime();
    if (std::isnan(normalizedEndTime)) {
        normalizedEndTime = EventSequence::defaultNormalizedEndTime(speedValue);
    }

    float startTime = normalizedStartTime;
    if (std::isnan(startTime)) {
        startTime = EventSequence::defaultNormalizedStartTime(speedValue);
    }

    return clip->length * (normalizedEndTime - startTime);
}

float ClipTransition::maximumDuration() const {
    return clip != nullptr ? clip->length : 0.0f;
}

bool ClipTransition::isLooping() const noexcept {
    return clip != nullptr && clip->looping;
}

float ClipTransition::averageAngularSpeed() const {
    return 0.0f;
}

Vec3 ClipTransition::averageVelocity() const {
    return clip != nullptr ? Vec3(0.0f, 0.0f, clip->averageSpeed) : Vec3(0.0f);
}

void ClipTransition::gatherClips(std::vector<const Clip*>& clips) const {
    gatherClip(clips, clip);
}

void ClipTransition::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    gatherClips(clips);
}

void ClipTransition::copyFrom(const ClipTransition& other) {
    if (&other == this) return;
    clip = other.clip;
    fadeDurationValue = other.fadeDurationValue;
    speedValue = other.speedValue;
    normalizedStartTime = other.normalizedStartTime;
    eventSequence.copyFrom(other.eventSequence);
}

}  // namespace anim
