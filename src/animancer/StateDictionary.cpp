#include "anim/animancer/StateDictionary.h"

#include "anim/animancer/AnimancerLayer.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/ClipState.h"
#include "anim/animancer/LayerList.h"
#include "anim/animancer/Transition.h"
#include "anim/clip/Clip.h"
#include "anim/core/Assert.h"

namespace anim {

StateDictionary::StateDictionary(AnimancerPlayable& root) : _root(&root) {}

void StateDictionary::registerState(AnimancerState* state) {
    if (state == nullptr) return;
    Key key = state->key();
    if (!key.valid()) {
        key = Key::fromPointer(state);
        state->setKey(key);
    }
    _states[key] = state;
}

void StateDictionary::unregister(AnimancerState* state) {
    if (state == nullptr) return;
    for (auto it = _states.begin(); it != _states.end();) {
        if (it->second == state)
            it = _states.erase(it);
        else
            ++it;
    }
}

bool StateDictionary::tryGet(Key key, AnimancerState*& state) const {
    auto it = _states.find(key);
    if (it == _states.end()) {
        state = nullptr;
        return false;
    }
    state = it->second;
    return true;
}

AnimancerState* StateDictionary::get(Key key) const {
    auto it = _states.find(key);
    return it == _states.end() ? nullptr : it->second;
}

AnimancerState* StateDictionary::current() const {
    if (_root == nullptr) return nullptr;
    AnimancerLayer* layer = _root->layers().getLayer(0);
    return layer != nullptr ? layer->currentState() : nullptr;
}

void StateDictionary::createIfNew(const std::vector<const Clip*>& clips) {
    for (const Clip* clip : clips) {
        if (clip != nullptr) getOrCreate(*clip);
    }
}

void StateDictionary::createIfNew(std::initializer_list<const Clip*> clips) {
    for (const Clip* clip : clips) {
        if (clip != nullptr) getOrCreate(*clip);
    }
}

AnimancerState* StateDictionary::getOrCreate(const Clip& clip, bool allowSetClip) {
    const Key key = Key::fromPointer(&clip);

    AnimancerState* existing = nullptr;
    if (tryGet(key, existing)) {
        if (existing->clip() != &clip) {
            if (!allowSetClip) {
                ANIM_CHECK_MSG(false, "the state registered for this key uses a different clip");
            }
            if (auto* clipState = dynamic_cast<ClipState*>(existing)) {
                clipState->setClip(clip);
            } else {
                ANIM_CHECK_MSG(false, "cannot set clip on a non-ClipState");
            }
        }
        return existing;
    }

    ANIM_CHECK_MSG(_root != nullptr, "state dictionary has no root");
    auto* state = new ClipState(_root, clip);
    registerState(state);
    return state;
}

AnimancerState* StateDictionary::getOrCreate(ITransition& transition) {
    AnimancerState* existing = nullptr;
    if (tryGet(transition.key(), existing)) return existing;

    ANIM_CHECK_MSG(_root != nullptr, "state dictionary has no root");
    AnimancerState* state = transition.createState(*_root);
    if (state == nullptr) return nullptr;

    state->setKey(transition.key());
    registerState(state);
    return state;
}

void StateDictionary::destroy(AnimancerState* state) {
    if (state != nullptr) state->destroy();
}

void StateDictionary::destroy(const Clip& clip) {
    AnimancerState* state = get(Key::fromPointer(&clip));
    if (state != nullptr) state->destroy();
}

void StateDictionary::destroyAll() {
    const std::vector<AnimancerState*> snapshot = states();
    for (AnimancerState* state : snapshot) {
        if (state != nullptr) state->destroy();
    }
}

std::vector<AnimancerState*> StateDictionary::states() const {
    std::vector<AnimancerState*> result;
    result.reserve(_states.size());
    for (const auto& entry : _states) result.push_back(entry.second);
    return result;
}

void StateDictionary::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    for (const auto& entry : _states) {
        if (entry.second != nullptr) entry.second->gatherAnimationClips(clips);
    }
}

}  // namespace anim
