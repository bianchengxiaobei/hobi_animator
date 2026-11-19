#include "anim/hobi/TimeSynchronizationGroup.h"

namespace anim {

TimeSynchronizationGroup::TimeSynchronizationGroup(HobiPlayable* playable)
    : _playable(playable) {}

void TimeSynchronizationGroup::setPlayable(HobiPlayable* playable) {
    _playable = playable;
    normalizedTime.reset();
}

bool TimeSynchronizationGroup::storeTime(Key key) {
    return storeTime(key, _playable != nullptr ? _playable->states().current() : nullptr);
}

bool TimeSynchronizationGroup::storeTime(Key key, HobiState* state) {
    if (state != nullptr && contains(key)) {
        normalizedTime = state->normalizedTime();
        return true;
    }

    normalizedTime.reset();
    return false;
}

bool TimeSynchronizationGroup::syncTime(Key key) {
    HobiState* state = _playable != nullptr ? _playable->states().current() : nullptr;
    return syncTime(key, state, deltaTimeOf(state));
}

bool TimeSynchronizationGroup::syncTime(Key key, float deltaTime) {
    HobiState* state = _playable != nullptr ? _playable->states().current() : nullptr;
    return syncTime(key, state, deltaTime);
}

bool TimeSynchronizationGroup::syncTime(Key key, HobiState* state) {
    return syncTime(key, state, deltaTimeOf(state));
}

bool TimeSynchronizationGroup::syncTime(Key key, HobiState* state, float deltaTime) {
    if (!normalizedTime.has_value() || state == nullptr || !contains(key)) return false;

    state->setTime(*normalizedTime * state->length() + deltaTime * state->effectiveSpeed());
    return true;
}

float TimeSynchronizationGroup::deltaTimeOf(const HobiState* state) const {
    if (_playable != nullptr) return _playable->deltaTime();
    if (state != nullptr && state->root() != nullptr) return state->root()->deltaTime();
    return 0.0f;
}

}  // namespace anim
