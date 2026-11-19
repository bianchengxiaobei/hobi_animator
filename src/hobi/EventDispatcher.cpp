#include "anim/hobi/EventDispatcher.h"

#include <cmath>

#include "anim/hobi/HobiState.h"
#include "anim/core/Assert.h"

namespace anim {
namespace {

float wrap01(float value) {
    float wrapped = std::fmod(value, 1.0f);
    if (wrapped < 0.0f) wrapped += 1.0f;
    return wrapped;
}

}  // namespace

EventDispatcher::EventDispatcher(HobiState& state) : _state(&state) {}

void EventDispatcher::setEvents(EventSequence* events) {
    _events = events;
    _nextEventIndex = kRecalculateIndex;
    _sequenceVersion = events != nullptr ? events->version() : -1;
    _released = false;
}

void EventDispatcher::onTimeChanged() {
    if (_state == nullptr) return;
    _previousTime = _state->normalizedTime();
    _nextEventIndex = kRecalculateIndex;
}

void EventDispatcher::release() {
    _events = nullptr;
    _nextEventIndex = kRecalculateIndex;
    _released = true;
}

void EventDispatcher::update(bool& needsMoreUpdates) {
    needsMoreUpdates = false;

    if (_events == nullptr || _events->isEmpty()) return;

    const float length = _state->length();
    if (length == 0.0f) {
        updateZeroLength();
        return;
    }

    const float currentTime = _state->time() / length;
    if (_previousTime == currentTime) return;

    _isLooping = _state->isLooping();

    // General events are triggered on the frame their time passes.
    checkGeneralEvents(currentTime);
    if (_events == nullptr) return;

    // End event is triggered every frame after its time passes.
    const HobiEvent& endEvent = _events->endEvent();
    if (endEvent.hasCallback()) {
        if (currentTime > _previousTime) {
            const float eventTime = std::isnan(endEvent.normalizedTime) ? 1.0f : endEvent.normalizedTime;
            if (currentTime > eventTime) endEvent.invoke();
        } else {
            const float eventTime = std::isnan(endEvent.normalizedTime) ? 0.0f : endEvent.normalizedTime;
            if (currentTime < eventTime) endEvent.invoke();
        }
    }

    if (_nextEventIndex != kRecalculateIndex) _previousTime = currentTime;
}

void EventDispatcher::updateZeroLength() {
    const float speed = _state->effectiveSpeed();
    if (speed == 0.0f) return;

    if (_events->count() > 0) {
        int playDirectionInt = 1;
        if (speed < 0.0f) {
            playDirectionInt = -1;
            if (_nextEventIndex == kRecalculateIndex || _sequenceVersion != _events->version() ||
                _wasPlayingForwards) {
                _nextEventIndex = _events->count() - 1;
                _sequenceVersion = _events->version();
                _wasPlayingForwards = false;
            }
        } else {
            if (_nextEventIndex == kRecalculateIndex || _sequenceVersion != _events->version() ||
                !_wasPlayingForwards) {
                _nextEventIndex = 0;
                _sequenceVersion = _events->version();
                _wasPlayingForwards = true;
            }
        }

        if (!invokeAllEvents(1, playDirectionInt)) return;
    }

    if (_events != nullptr) _events->endEvent().invoke();
}

void EventDispatcher::checkGeneralEvents(float currentTime) {
    const int count = _events->count();
    if (count == 0) {
        _nextEventIndex = 0;
        return;
    }

    float playDirectionFloat = 1.0f;
    int playDirectionInt = 1;
    validateNextEventIndex(currentTime, playDirectionFloat, playDirectionInt);

    if (_isLooping) {
        if (_nextEventIndex < 0 || _nextEventIndex >= count) return;

        float eventTime = _events->at(_nextEventIndex).normalizedTime * playDirectionFloat;
        const int loopDelta = getLoopDelta(_previousTime, currentTime, eventTime);
        if (loopDelta == 0) return;

        // Additional loops: invoke all events without checking times.
        if (!invokeAllEvents(loopDelta - 1, playDirectionInt)) return;

        const int loopStartIndex = _nextEventIndex;

        while (true) {
            if (_events == nullptr) return;
            _events->at(_nextEventIndex).invoke();

            if (!nextEventLooped(playDirectionInt) || _nextEventIndex == loopStartIndex) return;
            if (_events == nullptr) return;

            eventTime = _events->at(_nextEventIndex).normalizedTime * playDirectionFloat;
            if (loopDelta != getLoopDelta(_previousTime, currentTime, eventTime)) return;
        }
    } else {
        while (static_cast<unsigned>(_nextEventIndex) < static_cast<unsigned>(count)) {
            if (_events == nullptr) return;
            const HobiEvent& hobiEvent = _events->at(_nextEventIndex);
            const float eventTime = hobiEvent.normalizedTime * playDirectionFloat;

            if (currentTime <= eventTime) return;

            hobiEvent.invoke();

            if (!nextEvent(playDirectionInt)) return;
        }
    }
}

void EventDispatcher::validateNextEventIndex(float& currentTime, float& playDirectionFloat,
                                             int& playDirectionInt) {
    const int sequenceVersion = _events->version();

    if (currentTime < _previousTime) {
        // Playing backwards: negate both times.
        float previousTime = _previousTime;
        _previousTime = -previousTime;
        currentTime = -currentTime;
        playDirectionFloat = -1.0f;
        playDirectionInt = -1;

        if (_nextEventIndex == kRecalculateIndex || _sequenceVersion != sequenceVersion ||
            _wasPlayingForwards) {
            _nextEventIndex = _events->count() - 1;
            _sequenceVersion = sequenceVersion;
            _wasPlayingForwards = false;

            if (_isLooping) previousTime = wrap01(previousTime);

            while (_nextEventIndex >= 0 &&
                   _events->at(_nextEventIndex).normalizedTime > previousTime) {
                --_nextEventIndex;
                if (_nextEventIndex < 0) {
                    if (_isLooping) _nextEventIndex = _events->count() - 1;
                    break;
                }
            }
        }
    } else {
        // Playing forwards.
        playDirectionFloat = 1.0f;
        playDirectionInt = 1;

        if (_nextEventIndex == kRecalculateIndex || _sequenceVersion != sequenceVersion ||
            !_wasPlayingForwards) {
            _nextEventIndex = 0;
            _sequenceVersion = sequenceVersion;
            _wasPlayingForwards = true;

            float previousTime = _previousTime;
            if (_isLooping) previousTime = wrap01(previousTime);

            const int last = _events->count() - 1;
            while (_nextEventIndex <= last &&
                   _events->at(_nextEventIndex).normalizedTime < previousTime) {
                ++_nextEventIndex;
                if (_nextEventIndex > last) {
                    if (_isLooping) _nextEventIndex = 0;
                    break;
                }
            }
        }
    }
}

int EventDispatcher::getLoopDelta(float previousTime, float nextTime, float eventTime) {
    previousTime -= eventTime;
    nextTime -= eventTime;

    const int previousLoopCount = static_cast<int>(std::floor(previousTime));
    const int nextLoopCount = static_cast<int>(std::floor(nextTime));

    int loopCount = nextLoopCount - previousLoopCount;

    // Previous time is inclusive, next time is exclusive.
    if (previousTime == previousLoopCount) ++loopCount;
    if (nextTime == nextLoopCount) --loopCount;

    return loopCount;
}

bool EventDispatcher::invokeAllEvents(int count, int playDirectionInt) {
    const int loopStartIndex = _nextEventIndex;

    while (count-- > 0) {
        do {
            if (_events == nullptr) return false;
            _events->at(_nextEventIndex).invoke();

            if (!nextEventLooped(playDirectionInt)) return false;
        } while (_nextEventIndex != loopStartIndex);
    }

    return true;
}

bool EventDispatcher::nextEvent(int playDirectionInt) {
    if (_nextEventIndex == kRecalculateIndex) return false;
    _nextEventIndex += playDirectionInt;
    return true;
}

bool EventDispatcher::nextEventLooped(int playDirectionInt) {
    if (!nextEvent(playDirectionInt)) return false;
    if (_events == nullptr) return false;

    const int count = _events->count();
    if (count == 0) {
        _nextEventIndex = 0;
        return false;
    }
    if (_nextEventIndex >= count)
        _nextEventIndex = 0;
    else if (_nextEventIndex < 0)
        _nextEventIndex = count - 1;

    return true;
}

}  // namespace anim
