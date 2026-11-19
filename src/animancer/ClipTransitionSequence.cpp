#include "anim/animancer/ClipTransitionSequence.h"

#include <cmath>
#include <utility>

#include "anim/animancer/AnimancerLayer.h"
#include "anim/animancer/AnimancerState.h"

namespace anim {

ClipTransition& ClipTransitionSequence::lastTransition() noexcept {
    return others.empty() ? *this : others.back();
}

const ClipTransition& ClipTransitionSequence::lastTransition() const noexcept {
    return others.empty() ? *this : others.back();
}

AnimancerEvent& ClipTransitionSequence::endEvent() noexcept {
    return lastTransition().eventSequence.endEvent();
}

const AnimancerEvent& ClipTransitionSequence::endEvent() const noexcept {
    return lastTransition().eventSequence.endEvent();
}

void ClipTransitionSequence::EndCallback::operator()() const {
    if (owner != nullptr && state != nullptr) owner->playNext(state, index);
}

bool ClipTransitionSequence::isOwnEnd(const std::function<void()>& callback,
                                      const ClipTransitionSequence* owner) {
    const EndCallback* end = callback.target<EndCallback>();
    return end != nullptr && end->owner == owner;
}

void ClipTransitionSequence::installChain(AnimancerState* state) {
    EventSequence& firstEvents = eventSequence;
    AnimancerEvent& firstEnd = firstEvents.endEvent();

    if (!isOwnEnd(firstEnd.callback, this)) {
        std::function<void()> movedEnd = std::move(firstEnd.callback);
        firstEnd.callback = EndCallback{this, state, 0};
        if (movedEnd) {
            lastTransition().eventSequence.endEvent().callback = std::move(movedEnd);
        }
    } else {
        firstEnd.callback = EndCallback{this, state, 0};
    }

    for (std::size_t i = 1; i < others.size(); ++i) {
        others[i - 1].eventSequence.endEvent().callback =
            EndCallback{this, state, static_cast<int>(i)};
    }
}

void ClipTransitionSequence::playNext(AnimancerState* state, int index) {
    if (state == nullptr || index < 0 || static_cast<std::size_t>(index) >= others.size()) {
        return;
    }

    AnimancerLayer* layer = state->layer();
    if (layer != nullptr) layer->play(others[static_cast<std::size_t>(index)]);
}

void ClipTransitionSequence::apply(AnimancerState& state) {
    if (!others.empty()) installChain(&state);
    ClipTransition::apply(state);
}

bool ClipTransitionSequence::isValid() const {
    if (!ClipTransition::isValid()) return false;
    for (const ClipTransition& other : others) {
        if (!other.isValid()) return false;
    }
    return true;
}

bool ClipTransitionSequence::isLooping() const noexcept {
    return others.empty() ? ClipTransition::isLooping() : others.back().isLooping();
}

float ClipTransitionSequence::length() const {
    float value = ClipTransition::length();
    for (const ClipTransition& other : others) value += other.length();
    return value;
}

float ClipTransitionSequence::maximumDuration() const {
    float value = ClipTransition::maximumDuration();
    for (const ClipTransition& other : others) value += other.maximumDuration();
    return value;
}

float ClipTransitionSequence::averageAngularSpeed() const {
    float speed = ClipTransition::averageAngularSpeed();
    if (others.empty()) return speed;

    float duration = ClipTransition::maximumDuration();
    speed *= duration;

    for (const ClipTransition& other : others) {
        const float otherSpeed = other.averageAngularSpeed();
        const float otherDuration = other.maximumDuration();
        speed += otherSpeed * otherDuration;
        duration += otherDuration;
    }

    return speed / duration;
}

Vec3 ClipTransitionSequence::averageVelocity() const {
    Vec3 velocity = ClipTransition::averageVelocity();
    if (others.empty()) return velocity;

    float duration = ClipTransition::maximumDuration();
    velocity *= duration;

    for (const ClipTransition& other : others) {
        const Vec3 otherVelocity = other.averageVelocity();
        const float otherDuration = other.maximumDuration();
        velocity += otherVelocity * otherDuration;
        duration += otherDuration;
    }

    return velocity / duration;
}

void ClipTransitionSequence::gatherClips(std::vector<const Clip*>& clips) const {
    ClipTransition::gatherClips(clips);
    for (const ClipTransition& other : others) other.gatherClips(clips);
}

void ClipTransitionSequence::copyFrom(const ClipTransitionSequence& other) {
    if (&other == this) return;
    ClipTransition::copyFrom(other);
    others = other.others;
}

void ClipTransitionSequence::addEvent(float time, bool normalized,
                                      std::function<void()> callback) {
    if (normalized) time *= length();

    if (tryAddEvent(*this, ClipTransition::length(), time, callback)) return;

    for (std::size_t i = 0; i + 1 < others.size(); ++i) {
        if (tryAddEvent(others[i], others[i].length(), time, callback)) return;
    }

    addEventTo(lastTransition(), time, callback);
}

bool ClipTransitionSequence::tryAddEvent(ClipTransition& transition, float length, float& time,
                                         const std::function<void()>& callback) {
    if (time > length) {
        time -= length;
        return false;
    }

    addEventTo(transition, time, callback);
    return true;
}

void ClipTransitionSequence::addEventTo(ClipTransition& transition, float time,
                                        const std::function<void()>& callback) {
    if (transition.clip == nullptr) return;

    float start = transition.normalizedStartTime;
    if (std::isnan(start)) start = EventSequence::defaultNormalizedStartTime(start);

    time /= transition.clip->length * (1.0f - start);
    time += start;

    transition.eventSequence.add(time, callback);
}

}  // namespace anim
