#include "anim/hobi/HobiNode.h"

#include <cmath>
#include <limits>

#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/ExitEvent.h"
#include "anim/core/Assert.h"

namespace anim {

HobiNode::~HobiNode() { ExitEvent::unregisterAll(*this); }

const char* toString(FadeMode mode) noexcept {
    switch (mode) {
        case FadeMode::FixedSpeed: return "FixedSpeed";
        case FadeMode::FixedDuration: return "FixedDuration";
        case FadeMode::FromStart: return "FromStart";
        case FadeMode::NormalizedSpeed: return "NormalizedSpeed";
        case FadeMode::NormalizedDuration: return "NormalizedDuration";
        case FadeMode::NormalizedFromStart: return "NormalizedFromStart";
    }
    return "Unknown";
}

PlayableGraph* HobiNode::graph() const noexcept {
    return _root != nullptr ? &_root->graph() : nullptr;
}

void HobiNode::setWeight(float value) {
    if (_weight == value) return;
    ANIM_CHECK_MSG(std::isfinite(value) && value >= 0.0f,
                   "Weight must be a finite positive value");
    _weight = value;
    setWeightDirty();
    _targetWeight = value;
    _fadeSpeed = 0.0f;
}

void HobiNode::setWeightOnly(float value) {
    if (_weight == value) return;
    ANIM_CHECK_MSG(std::isfinite(value) && value >= 0.0f,
                   "Weight must be a finite positive value");
    _weight = value;
    setWeightDirty();
}

void HobiNode::setWeightDirty() noexcept {
    _weightDirty = true;
    if (_root != nullptr) _root->requirePreUpdate(this);
}

float HobiNode::effectiveWeight() const noexcept {
    float weight = _weight;
    for (const HobiNode* parent = _parent; parent != nullptr; parent = parent->parent()) {
        weight *= parent->weight();
    }
    return weight;
}

void HobiNode::applyWeight() {
    if (!_weightDirty) return;
    _weightDirty = false;

    if (_parent == nullptr) return;

    if (!_parent->keepChildrenConnected() && _weight == 0.0f) {
        disconnectFromGraph();
        return;
    }

    if (!_playable.valid()) createPlayable();
    if (!_connected) connectToGraph();

    PlayableGraph* g = graph();
    if (g == nullptr || !_parent->playable().valid()) return;
    g->setInputWeight(_parent->playable(), _index, _weight);
}

void HobiNode::startFade(float targetWeight) {
    const float duration = _root != nullptr ? _root->defaultFadeDuration : 0.25f;
    startFade(targetWeight, duration);
}

void HobiNode::startFade(float targetWeight, float duration) {
    _targetWeight = targetWeight;

    if (targetWeight == _weight) {
        if (targetWeight == 0.0f) {
            stop();
        } else {
            _fadeSpeed = 0.0f;
            onStartFade();
        }
        return;
    }

    if (duration <= 0.0f) {
        _fadeSpeed = std::numeric_limits<float>::infinity();
    } else {
        _fadeSpeed = std::abs(_weight - targetWeight) / duration;
    }

    onStartFade();
    if (_root != nullptr) _root->requirePreUpdate(this);
}

void HobiNode::stop() { setWeight(0.0f); }

void HobiNode::setSpeed(float value) {
    ANIM_CHECK_MSG(std::isfinite(value), "Speed must be finite");
    _speed = value;
}

float HobiNode::effectiveSpeed() const noexcept {
    float speed = _speed * _playbackSpeedScale;
    for (const HobiNode* parent = _parent; parent != nullptr; parent = parent->parent()) {
        speed *= parent->_speed * parent->_playbackSpeedScale;
    }
    return speed;
}

void HobiNode::setEffectiveSpeed(float value) {
    float parentSpeed = 1.0f;
    for (const HobiNode* parent = _parent; parent != nullptr; parent = parent->parent()) {
        parentSpeed *= parent->speed();
    }
    setSpeed(parentSpeed != 0.0f ? value / parentSpeed : value);
}

void HobiNode::createPlayable() {}

void HobiNode::destroyPlayable() {
    if (_root != nullptr && _playable.valid()) {
        _root->graph().destroy(_playable);
    }
    _playable = PlayableHandle{};
    _connected = false;
}

void HobiNode::connectToGraph() {
    if (_parent == nullptr || !_parent->playable().valid()) return;
    if (!_playable.valid()) createPlayable();
    if (!_playable.valid()) return;

    PlayableGraph* g = graph();
    if (g == nullptr) return;

    g->connect(_playable, 0, _parent->playable(), _index);
    _connected = true;
}

void HobiNode::disconnectFromGraph() {
    if (!_connected) return;
    PlayableGraph* g = graph();
    if (g != nullptr && _parent != nullptr && _parent->playable().valid()) {
        g->disconnect(_parent->playable(), _index);
    }
    _connected = false;
}

void HobiNode::updateFade(bool& needsMoreUpdates) {
    if (_fadeSpeed == 0.0f) {
        needsMoreUpdates = false;
        return;
    }

    _weightDirty = true;

    const float parentSpeed = _parent != nullptr ? _parent->effectiveSpeed() : 1.0f;
    const float deltaTime = _root != nullptr ? _root->deltaTime() : 0.0f;
    float step = std::abs(_fadeSpeed * parentSpeed * deltaTime);

    const float target = _targetWeight;
    const float current = _weight;
    const float delta = target - current;

    if (delta > 0.0f) {
        if (delta > step) {
            _weight = current + step;
            needsMoreUpdates = true;
            return;
        }
    } else {
        if (-delta > step) {
            _weight = current - step;
            needsMoreUpdates = true;
            return;
        }
    }

    _weight = target;
    needsMoreUpdates = false;

    if (target == 0.0f) {
        stop();
    } else {
        _fadeSpeed = 0.0f;
    }
}

void HobiNode::update(bool& needsMoreUpdates) {
    updateFade(needsMoreUpdates);
    applyWeight();
}

}  // namespace anim
