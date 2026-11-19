#include "anim/animancer/DontAllowFade.h"

#include <cstdio>
#include <memory>

#include "anim/animancer/AnimancerLayer.h"
#include "anim/animancer/AnimancerNode.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/LayerList.h"

namespace anim {

DontAllowFade::DontAllowFade(AnimancerPlayable& playable) : _playable(&playable) {}

void DontAllowFade::enable(AnimancerPlayable& playable) {
    playable.requireOwnedPreUpdate(std::make_unique<DontAllowFade>(playable));
}

void DontAllowFade::assertEnabled(AnimancerPlayable& playable) {
#ifndef NDEBUG
    enable(playable);
#else
    (void)playable;
#endif
}

void DontAllowFade::validate(AnimancerNode& node) {
    if (node.fadeSpeed() == 0.0f) return;

#ifndef NDEBUG
    std::fprintf(stderr, "DontAllowFade: cancelling an active fade\n");
#endif

    const float target = node.targetWeight();
    node.setWeightOnly(target);
    node.setTargetWeight(target);
    node.setFadeSpeed(0.0f);
    node.applyWeight();
}

void DontAllowFade::update(bool& needsMoreUpdates) {
    needsMoreUpdates = false;
    if (_playable == nullptr) return;

    LayerList& layers = _playable->layers();
    for (int i = layers.count() - 1; i >= 0; --i) {
        AnimancerLayer* layer = layers.getLayer(i);
        if (layer == nullptr) continue;

        validate(*layer);
        if (layer->currentState() != nullptr) validate(*layer->currentState());
    }
}

}  // namespace anim
