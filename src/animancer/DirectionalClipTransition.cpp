#include "anim/animancer/DirectionalClipTransition.h"

#include "anim/animancer/AnimationClipCollection.h"

namespace anim {

void DirectionalClipTransition::setDirection(Vec2 direction) {
    if (animationSet == nullptr) {
        clip = nullptr;
        return;
    }
    clip = animationSet->getClip(direction);
}

void DirectionalClipTransition::setDirection(int direction) {
    if (animationSet == nullptr) {
        clip = nullptr;
        return;
    }
    clip = animationSet->getClip(direction);
}

void DirectionalClipTransition::setDirection(DirectionalAnimationSet::Direction direction) {
    if (animationSet == nullptr) {
        clip = nullptr;
        return;
    }
    clip = animationSet->getClip(direction);
}

void DirectionalClipTransition::setDirection(DirectionalAnimationSet8::Direction direction) {
    if (animationSet == nullptr) {
        clip = nullptr;
        return;
    }
    clip = animationSet->getClip(static_cast<int>(direction));
}

void DirectionalClipTransition::gatherAnimationClips(
    std::vector<const Clip*>& clips) const {
    ClipTransition::gatherAnimationClips(clips);
    if (animationSet != nullptr) animationSet->gatherAnimationClips(clips);
}

void DirectionalClipTransition::copyFrom(const DirectionalClipTransition& other) {
    if (&other == this) return;
    ClipTransition::copyFrom(other);
    animationSet = other.animationSet;
}

}  // namespace anim
