#include "anim/hobi/HobiState.h"

#include <cmath>
#include <limits>

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/EventDispatcher.h"
#include "anim/hobi/StateDictionary.h"
#include "anim/core/Assert.h"

namespace anim {

HobiState::HobiState(HobiPlayable* root) {
    setRoot(root);
    _dispatcher = std::make_unique<EventDispatcher>(*this);
}

HobiState::~HobiState() = default;

HobiLayer* HobiState::layer() const noexcept {
    return dynamic_cast<HobiLayer*>(parent());
}

void HobiState::setIsPlaying(bool value) {
    if (_isPlaying == value) return;

    _isPlaying = value;
    onSetIsPlaying();

    // 如果已经是 dirty，说明只是回到之前的状态，取消 dirty。
    if (_isPlayingDirty) {
        _isPlayingDirty = false;
    } else {
        _isPlayingDirty = true;
        if (root() != nullptr) root()->requirePreUpdate(this);
    }
}

void HobiState::play() {
    setIsPlaying(true);
    setWeight(1.0f);
    if (_automaticallyClearEvents) clearEvents();
}

void HobiState::stop() {
    HobiNode::stop();
    setIsPlaying(false);
    setTimeD(0.0);
    if (_automaticallyClearEvents) clearEvents();
}

void HobiState::setTimeD(double value) {
    ANIM_CHECK_MSG(std::isfinite(value), "Time must be finite");
    _time = value;

    HobiPlayable* root = this->root();
    if (root == nullptr) {
        _mustSetTime = true;
    } else if (root->isRunningPostUpdate()) {
        // 事件回调里改时间：延迟到下一次 pre-update，保证 Time 与 Weight 同步。
        _mustSetTime = true;
        root->requirePreUpdate(this);
    } else {
        applyTimeToPlayable();
    }

    if (_dispatcher) _dispatcher->onTimeChanged();
}

void HobiState::applyTimeToPlayable() {
    if (root() != nullptr && playable().valid()) {
        root()->setNodeTime(playable(), _time);
    }
}

void HobiState::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    gatherClip(clips, clip());

    for (int i = childCount() - 1; i >= 0; --i) {
        const HobiState* child = getChild(i);
        if (child != nullptr) child->gatherAnimationClips(clips);
    }
}

void HobiState::setTimeOnly(double value) {
    _time = value;
    applyTimeToPlayable();
}

double HobiState::normalizedTimeD() const {
    const float len = length();
    if (len <= 0.0f) return 0.0;
    return _time / static_cast<double>(len);
}

void HobiState::setNormalizedTimeD(double value) {
    setTimeD(value * static_cast<double>(length()));
}

void HobiState::moveTime(double time, bool normalized) {
    setTimeD(normalized ? time * static_cast<double>(length()) : time);
}

float HobiState::normalizedEndTime() const {
    if (_events != nullptr) {
        const float value = _events->normalizedEndTime();
        if (!std::isnan(value)) return value;
    }
    return EventSequence::defaultNormalizedEndTime(effectiveSpeed());
}

void HobiState::setNormalizedEndTime(float value) { ensureEvents().setNormalizedEndTime(value); }

float HobiState::duration() const {
    const float speed = effectiveSpeed();
    if (_events != nullptr) {
        const float endTime = _events->normalizedEndTime();
        if (!std::isnan(endTime)) {
            if (speed > 0.0f) return length() * endTime / speed;
            if (speed < 0.0f) return length() * (1.0f - endTime) / -speed;
        }
    }
    const float absSpeed = std::abs(speed);
    return absSpeed > 0.0f ? length() / absSpeed : std::numeric_limits<float>::infinity();
}

void HobiState::setDuration(float value) {
    float lengthValue = length();
    if (_events != nullptr) {
        const float endTime = _events->normalizedEndTime();
        if (!std::isnan(endTime)) {
            if (effectiveSpeed() > 0.0f)
                lengthValue *= endTime;
            else
                lengthValue *= 1.0f - endTime;
        }
    }
    setEffectiveSpeed(value != 0.0f ? lengthValue / value : 0.0f);
}

float HobiState::remainingDuration() const {
    const float speed = effectiveSpeed();
    if (speed == 0.0f) return std::numeric_limits<float>::infinity();
    return (length() * normalizedEndTime() - time()) / speed;
}

void HobiState::setRemainingDuration(float value) {
    if (value == 0.0f) {
        setEffectiveSpeed(0.0f);
        return;
    }
    setEffectiveSpeed((length() * normalizedEndTime() - time()) / value);
}

void HobiState::setEvents(EventSequence* events) {
    _events = events;
    if (_dispatcher == nullptr) _dispatcher = std::make_unique<EventDispatcher>(*this);
    _dispatcher->setEvents(events);
    if (events != nullptr && root() != nullptr) root()->requirePostUpdate(_dispatcher.get());
}

EventSequence& HobiState::ensureEvents() {
    if (_events == nullptr) {
        _ownedEvents = std::make_unique<EventSequence>();
        setEvents(_ownedEvents.get());
    }
    return *_events;
}

void HobiState::clearEvents() {
    if (_events != nullptr) _events->clear();
    if (_dispatcher != nullptr) _dispatcher->onTimeChanged();
}

void HobiState::onStartFade() {
    if (_automaticallyClearEvents) clearEvents();
}

void HobiState::update(bool& needsMoreUpdates) {
    HobiPlayable* root = this->root();

    // 时间每帧只推进一次（pre-update 在本帧可能被多次调用以完成淡入淡出）。
    if (root != nullptr && _lastTimeUpdateFrame != root->frameId()) {
        _lastTimeUpdateFrame = root->frameId();
        if (_isPlaying && root->isGraphPlaying()) {
            _time += static_cast<double>(root->deltaTime()) * static_cast<double>(effectiveSpeed());
        }
    }

    HobiNode::update(needsMoreUpdates);

    if (root != nullptr && playable().valid()) {
        if (_isPlayingDirty) {
            _isPlayingDirty = false;
            root->setNodePlaying(playable(), _isPlaying);
        }
        // 状态时间是唯一权威：每帧同步到图节点（ClipNode 自己不再推进时间）。
        root->setNodeTime(playable(), _time);
        _mustSetTime = false;
    }
}

void HobiState::destroy() {
    HobiPlayable* root = this->root();

    disconnectFromGraph();

    if (root != nullptr) {
        root->cancelPreUpdate(this);
        if (_dispatcher) root->cancelPostUpdate(_dispatcher.get());
        root->states().unregister(this);
    }

    if (HobiLayer* layer = this->layer()) layer->removeChild(this);

    destroyPlayable();
    delete this;
}

}  // namespace anim
