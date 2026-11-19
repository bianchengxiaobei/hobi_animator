#include "anim/hobi/Event.h"

#include <algorithm>

#include "anim/core/Assert.h"

namespace anim {

HobiEvent& EventSequence::at(int index) {
    ANIM_CHECK_MSG(index >= 0 && index < count(), "event index out of range");
    return _events[static_cast<std::size_t>(index)];
}

const HobiEvent& EventSequence::at(int index) const {
    ANIM_CHECK_MSG(index >= 0 && index < count(), "event index out of range");
    return _events[static_cast<std::size_t>(index)];
}

int EventSequence::add(HobiEvent event) {
    // 稳定插入：相同时间的新事件排在已有事件之后。
    std::size_t index = 0;
    while (index < _events.size() && _events[index].normalizedTime <= event.normalizedTime) {
        ++index;
    }
    _events.insert(_events.begin() + static_cast<std::ptrdiff_t>(index), std::move(event));
    markModified();
    return static_cast<int>(index);
}

int EventSequence::add(float normalizedTime, std::function<void()> callback, std::string name) {
    return add(HobiEvent(normalizedTime, std::move(callback), std::move(name)));
}

void EventSequence::addCallback(int index, std::function<void()> callback) {
    if (index == count()) {
        _endEvent.callback = std::move(callback);
        return;
    }
    at(index).callback = std::move(callback);
}

void EventSequence::remove(int index) {
    ANIM_CHECK_MSG(index >= 0 && index < count(), "event index out of range");
    _events.erase(_events.begin() + index);
    markModified();
}

void EventSequence::clear() {
    _events.clear();
    _endEvent = HobiEvent(std::numeric_limits<float>::quiet_NaN(), nullptr);
    markModified();
}

int EventSequence::indexOf(std::string_view name) const {
    for (int i = 0; i < count(); ++i) {
        if (_events[static_cast<std::size_t>(i)].name == name) return i;
    }
    return -1;
}

int EventSequence::indexOfRequired(std::string_view name) const {
    const int index = indexOf(name);
    ANIM_CHECK_MSG(index >= 0, "no event with the requested name");
    return index;
}

void EventSequence::copyFrom(const EventSequence& other) {
    _events = other._events;
    _endEvent = other._endEvent;
    markModified();
}

}  // namespace anim
