#include "anim/animancer/TimeSynchronizationGroup.h"

namespace anim {

TimeSynchronizationGroup::TimeSynchronizationGroup(AnimancerPlayable* animancer)
    : _animancer(animancer) {}

void TimeSynchronizationGroup::setAnimancer(AnimancerPlayable* animancer) {
    _animancer = animancer;
    normalizedTime.reset();
}

bool TimeSynchronizationGroup::storeTime(Key key) {
    return storeTime(key, _animancer != nullptr ? _animancer->states().current() : nullptr);
}

bool TimeSynchronizationGroup::storeTime(Key key, AnimancerState* state) {
    if (state != nullptr && contains(key)) {
        normalizedTime = state->normalizedTime();
        return true;
    }

    normalizedTime.reset();
    return false;
}

bool TimeSynchronizationGroup::syncTime(Key key) {
    AnimancerState* state = _animancer != nullptr ? _animancer->states().current() : nullptr;
    return syncTime(key, state, deltaTimeOf(state));
}

bool TimeSynchronizationGroup::syncTime(Key key, float deltaTime) {
    AnimancerState* state = _animancer != nullptr ? _animancer->states().current() : nullptr;
    return syncTime(key, state, deltaTime);
}

bool TimeSynchronizationGroup::syncTime(Key key, AnimancerState* state) {
    return syncTime(key, state, deltaTimeOf(state));
}

bool TimeSynchronizationGroup::syncTime(Key key, AnimancerState* state, float deltaTime) {
    if (!normalizedTime.has_value() || state == nullptr || !contains(key)) return false;

    state->setTime(*normalizedTime * state->length() + deltaTime * state->effectiveSpeed());
    return true;
}

float TimeSynchronizationGroup::deltaTimeOf(const AnimancerState* state) const {
    if (_animancer != nullptr) return _animancer->deltaTime();
    if (state != nullptr && state->root() != nullptr) return state->root()->deltaTime();
    return 0.0f;
}

}  // namespace anim
