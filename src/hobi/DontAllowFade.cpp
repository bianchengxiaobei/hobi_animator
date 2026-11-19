#include "anim/hobi/DontAllowFade.h"

#include <cstdio>
#include <memory>

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiNode.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/LayerList.h"

namespace anim {

DontAllowFade::DontAllowFade(HobiPlayable& playable) : _playable(&playable) {}

void DontAllowFade::enable(HobiPlayable& playable) {
    playable.requireOwnedPreUpdate(std::make_unique<DontAllowFade>(playable));
}

void DontAllowFade::assertEnabled(HobiPlayable& playable) {
#ifndef NDEBUG
    enable(playable);
#else
    (void)playable;
#endif
}

void DontAllowFade::validate(HobiNode& node) {
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
        HobiLayer* layer = layers.getLayer(i);
        if (layer == nullptr) continue;

        validate(*layer);
        if (layer->currentState() != nullptr) validate(*layer->currentState());
    }
}

}  // namespace anim
