#include "anim/integration/AnimationTransitions.h"

#include <cmath>
#include <utility>

#include "anim/animancer/AnimancerAnimator.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/core/Hash.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {

bool AnimationTransitions::build(const AnimationClipLibrary& library,
                                 const EventCallbackMap& callbacks, std::string& error) {
    error.clear();

    std::vector<Entry> entries;
    entries.reserve(static_cast<std::size_t>(library.count()));

    for (int i = 0; i < library.count(); ++i) {
        const ClipAssetInfo* info = library.info(i);
        const Clip* clip = library.clip(i);
        if (info == nullptr || clip == nullptr) {
            error = "animation clip library entry " + std::to_string(i) + " is invalid";
            return false;
        }

        if (info->name.empty()) {
            error = "animation clip library entry " + std::to_string(i) + " has no name";
            return false;
        }

        for (const Entry& existing : entries) {
            if (existing.info.name == info->name) {
                error = "duplicate animation transition name '" + info->name + "'";
                return false;
            }
        }

        if (!std::isfinite(info->fadeDuration) || info->fadeDuration < 0.0f) {
            error = "animation clip '" + info->name +
                    "' has an invalid fade duration";
            return false;
        }

        if (!std::isfinite(info->speed) || info->speed == 0.0f) {
            error = "animation clip '" + info->name + "' has an invalid speed";
            return false;
        }

        if (!std::isnan(info->normalizedStartTime) &&
            (info->normalizedStartTime < 0.0f || info->normalizedStartTime > 1.0f)) {
            error = "animation clip '" + info->name +
                    "' has an invalid normalizedStartTime";
            return false;
        }

        auto transition = std::make_unique<ClipTransition>();
        transition->clip = clip;
        transition->fadeDurationValue = info->fadeDuration;
        transition->speedValue = info->speed;
        transition->normalizedStartTime = info->normalizedStartTime;

        for (const ClipEventInfo& event : info->events) {
            if (event.name.empty()) {
                error = "animation clip '" + info->name + "' has an event with no name";
                return false;
            }
            if (!std::isfinite(event.normalizedTime) || event.normalizedTime < 0.0f ||
                event.normalizedTime > 1.0f) {
                error = "animation clip '" + info->name +
                        "' has an event with an invalid normalizedTime";
                return false;
            }
            if (clip->looping && event.normalizedTime >= 1.0f) {
                error = "looping animation clip '" + info->name +
                        "' has an event at or after the end";
                return false;
            }

            const auto callback = callbacks.find(event.name);
            if (callback == callbacks.end() || !callback->second) continue;

            transition->eventSequence.add(event.normalizedTime, callback->second, event.name);
        }

        entries.push_back(Entry{*info, std::move(transition)});
    }

    _entries = std::move(entries);
    return true;
}

void AnimationTransitions::clear() noexcept { _entries.clear(); }

ClipTransition* AnimationTransitions::find(std::string_view name) noexcept {
    for (Entry& entry : _entries) {
        if (entry.info.name == name) return entry.transition.get();
    }
    return nullptr;
}

const ClipTransition* AnimationTransitions::find(std::string_view name) const noexcept {
    for (const Entry& entry : _entries) {
        if (entry.info.name == name) return entry.transition.get();
    }
    return nullptr;
}

const ClipAssetInfo* AnimationTransitions::info(std::string_view name) const noexcept {
    for (const Entry& entry : _entries) {
        if (entry.info.name == name) return &entry.info;
    }
    return nullptr;
}

const ClipAssetInfo* AnimationTransitions::info(int index) const noexcept {
    if (index < 0 || index >= count()) return nullptr;
    return &_entries[static_cast<std::size_t>(index)].info;
}

ClipTransition* AnimationTransitions::transition(int index) noexcept {
    if (index < 0 || index >= count()) return nullptr;
    return _entries[static_cast<std::size_t>(index)].transition.get();
}

const ClipTransition* AnimationTransitions::transition(int index) const noexcept {
    if (index < 0 || index >= count()) return nullptr;
    return _entries[static_cast<std::size_t>(index)].transition.get();
}

AnimancerState* AnimationTransitions::play(AnimancerAnimator& animator,
                                           std::string_view name, std::string* error) {
    if (error != nullptr) error->clear();

    ClipTransition* transition = find(name);
    if (transition == nullptr) {
        if (error != nullptr) {
            *error = "no animation transition named '" + std::string(name) + "'";
        }
        return nullptr;
    }

    if (!bindRootBone(animator, name, error)) return nullptr;

    AnimancerState* state = animator.play(*transition);
    if (state == nullptr && error != nullptr) {
        *error = "cannot create a state for animation '" + std::string(name) + "'";
    }
    return state;
}

bool AnimationTransitions::bindRootBone(AnimancerAnimator& animator, std::string_view name,
                                        std::string* error) const {
    if (error != nullptr) error->clear();

    const ClipAssetInfo* asset = info(name);
    if (asset == nullptr) {
        if (error != nullptr) {
            *error = "no animation transition named '" + std::string(name) + "'";
        }
        return false;
    }

    Skeleton* skeleton = animator.skeleton();
    if (skeleton == nullptr) {
        if (error != nullptr) {
            *error = "cannot bind a root bone because the animator has no skeleton";
        }
        return false;
    }

    if (asset->rootBone.empty()) {
        animator.playable().setRootMotionRootBone(-1);
        return true;
    }

    const int index = skeleton->find(hashString(asset->rootBone));
    if (index < 0) {
        if (error != nullptr) {
            *error = "animation clip '" + asset->name + "' rootBone '" + asset->rootBone +
                     "' was not found in the skeleton";
        }
        return false;
    }

    animator.playable().setRootMotionRootBone(index);
    return true;
}

}

