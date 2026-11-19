#include "anim/hobi/StateDictionary.h"

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/ClipState.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/Transition.h"
#include "anim/clip/Clip.h"
#include "anim/core/Assert.h"

namespace anim {

StateDictionary::StateDictionary(HobiPlayable& root) : _root(&root) {}

void StateDictionary::registerState(HobiState* state) {
    if (state == nullptr) return;
    Key key = state->key();
    if (!key.valid()) {
        key = Key::fromPointer(state);
        state->setKey(key);
    }
    _states[key] = state;
}

void StateDictionary::unregister(HobiState* state) {
    if (state == nullptr) return;
    for (auto it = _states.begin(); it != _states.end();) {
        if (it->second == state)
            it = _states.erase(it);
        else
            ++it;
    }
}

bool StateDictionary::tryGet(Key key, HobiState*& state) const {
    auto it = _states.find(key);
    if (it == _states.end()) {
        state = nullptr;
        return false;
    }
    state = it->second;
    return true;
}

HobiState* StateDictionary::get(Key key) const {
    auto it = _states.find(key);
    return it == _states.end() ? nullptr : it->second;
}

HobiState* StateDictionary::current() const {
    if (_root == nullptr) return nullptr;
    HobiLayer* layer = _root->layers().getLayer(0);
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

HobiState* StateDictionary::getOrCreate(const Clip& clip, bool allowSetClip) {
    const Key key = Key::fromPointer(&clip);

    HobiState* existing = nullptr;
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

HobiState* StateDictionary::getOrCreate(ITransition& transition) {
    HobiState* existing = nullptr;
    if (tryGet(transition.key(), existing)) return existing;

    ANIM_CHECK_MSG(_root != nullptr, "state dictionary has no root");
    HobiState* state = transition.createState(*_root);
    if (state == nullptr) return nullptr;

    state->setKey(transition.key());
    registerState(state);
    return state;
}

void StateDictionary::destroy(HobiState* state) {
    if (state != nullptr) state->destroy();
}

void StateDictionary::destroy(const Clip& clip) {
    HobiState* state = get(Key::fromPointer(&clip));
    if (state != nullptr) state->destroy();
}

void StateDictionary::destroyAll() {
    const std::vector<HobiState*> snapshot = states();
    for (HobiState* state : snapshot) {
        if (state != nullptr) state->destroy();
    }
}

std::vector<HobiState*> StateDictionary::states() const {
    std::vector<HobiState*> result;
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
