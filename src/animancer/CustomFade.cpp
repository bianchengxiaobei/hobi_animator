#include "anim/animancer/CustomFade.h"

#include <cmath>
#include <utility>

#include "anim/animancer/AnimancerLayer.h"
#include "anim/animancer/AnimancerNode.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/LayerList.h"
#include "anim/core/Assert.h"

namespace anim {

CustomFade::CustomFade(Curve curve) : _curve(std::move(curve)) {}

void CustomFade::apply(AnimancerNode& node, Curve curve) {
    if (!curve) return;
    (new CustomFade(std::move(curve)))->applyNode(node);
}

void CustomFade::apply(AnimancerState& state, Curve curve) {
    if (!curve) return;

    auto* fade = new CustomFade(std::move(curve));
    fade->applyNode(state);

    AnimancerNode* parent = state.parent();
    if (parent == nullptr) return;

    for (int i = parent->childCount() - 1; i >= 0; --i) {
        AnimancerNode* other = parent->getChild(i);
        if (other == nullptr || other == &state || other->fadeSpeed() == 0.0f) continue;

        other->setFadeSpeed(0.0f);
        fade->_fadeOutNodes.push_back({other, other->weight()});
    }
}

void CustomFade::apply(AnimancerPlayable& playable, Curve curve) {
    AnimancerLayer* layer = playable.layers()[0];
    if (layer == nullptr || layer->currentState() == nullptr) return;
    apply(*layer->currentState(), std::move(curve));
}

void CustomFade::applyNode(AnimancerNode& node) {
    ANIM_CHECK_MSG(node.parent() != nullptr, "Node is not connected to a layer");
    ANIM_CHECK_MSG(node.fadeSpeed() != 0.0f, "Node is not fading (FadeSpeed is 0)");

    _time = 0.0f;
    _target = {&node, node.weight()};
    _fadeSpeed = node.fadeSpeed();
    _layer = node.layer();
    _commandCount = _layer != nullptr ? _layer->commandCount() : 0;

    node.setFadeSpeed(0.0f);
    _fadeOutNodes.clear();

    if (node.root() != nullptr) node.root()->requirePreUpdate(this);
}

float CustomFade::calculateWeight(float progress) const {
    return _curve ? _curve(progress) : progress;
}

void CustomFade::update(bool& needsMoreUpdates) {
    needsMoreUpdates = false;

    // 状态被销毁、切换层或层播放了其它状态 → 取消（CustomFade.cs:141-151）。
    if (_layer == nullptr || _target.node == nullptr || _target.node->layer() != _layer ||
        _layer->commandCount() != _commandCount) {
        cancel();
        return;
    }

    AnimancerPlayable* root = _layer->root();
    if (root == nullptr) {
        cancel();
        return;
    }

    _time += root->deltaTime() * _layer->speed() * _fadeSpeed;

    if (_time < 1.0f) {
        const float weight = calculateWeight(_time);

        const float starting = _target.startingWeight;
        const float target = _target.node->targetWeight();
        _target.node->setWeightOnly(starting + (target - starting) * weight);
        _target.node->applyWeight();

        const float inverse = 1.0f - weight;
        for (NodeWeight& fadeOut : _fadeOutNodes) {
            if (fadeOut.node == nullptr) continue;
            fadeOut.node->setWeightOnly(fadeOut.startingWeight * inverse);
            fadeOut.node->applyWeight();
        }

        needsMoreUpdates = true;
        return;
    }

    _time = 1.0f;
    finishNode(*_target.node);
    for (NodeWeight& fadeOut : _fadeOutNodes) {
        if (fadeOut.node != nullptr) finishNode(*fadeOut.node);
    }
    _fadeOutNodes.clear();

    cancel();
}

void CustomFade::finishNode(AnimancerNode& node) {
    const float weight = node.targetWeight();
    node.setWeight(weight);
    node.applyWeight();
    if (weight == 0.0f) node.stop();
}

void CustomFade::cancel() {
    AnimancerPlayable* root = _layer != nullptr ? _layer->root() : nullptr;
    if (root != nullptr) root->cancelPreUpdate(this);
    delete this;
}

}  // namespace anim
