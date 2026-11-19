#include "anim/hobi/HobiLayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/ClipState.h"
#include "anim/hobi/StateDictionary.h"
#include "anim/hobi/Transition.h"
#include "anim/core/Assert.h"
#include "anim/graph/Nodes.h"

namespace anim {

float HobiLayer::weightlessThreshold = 0.1f;
int HobiLayer::maxCloneCount = 3;

HobiLayer::HobiLayer(HobiPlayable* root, int index, PlayableHandle mixerNode)
    : _mixerNode(mixerNode) {
    setRoot(root);
    setIndex(index);
    setPlayable(mixerNode);
    _weight = 1.0f;
    _targetWeight = 1.0f;
    _weightDirty = true;
}

HobiState* HobiLayer::getChild(int index) const {
    if (index < 0 || index >= childCount()) return nullptr;
    return _states[static_cast<std::size_t>(index)];
}

void HobiLayer::addChild(HobiState* state) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");
    if (state->parent() == this) return;

    state->setParent(this);
    state->setRoot(root());
    state->setIndex(childCount());
    _states.push_back(state);

    if (graph() != nullptr) graph()->setInputCount(_mixerNode, childCount());
    state->setWeightDirty();
}

void HobiLayer::removeChild(HobiState* state) {
    auto it = std::find(_states.begin(), _states.end(), state);
    if (it == _states.end()) return;

    const std::ptrdiff_t index = it - _states.begin();
    state->disconnectFromGraph();
    state->setParent(nullptr);
    state->setIndex(-1);

    // 对齐原项目：用最后一个填充空位，避免整体搬移。
    HobiState* moved = nullptr;
    if (index != static_cast<std::ptrdiff_t>(_states.size()) - 1) {
        moved = _states.back();
        moved->disconnectFromGraph();
        _states[static_cast<std::size_t>(index)] = moved;
        moved->setIndex(static_cast<int>(index));
    }
    _states.pop_back();

    if (_currentState == state) _currentState = nullptr;
    if (graph() != nullptr) graph()->setInputCount(_mixerNode, childCount());
    if (moved != nullptr &&
        (moved->weight() > 0.0f || moved->targetWeight() > 0.0f)) {
        moved->connectToGraph();
        moved->setWeightDirty();
    }
}

void HobiLayer::destroyStates() {
    while (!_states.empty()) {
        _states.back()->destroy();
    }
}

ClipState* HobiLayer::createState(const Clip& clip) {
    return createState(Key::fromPointer(&clip), clip);
}

ClipState* HobiLayer::createState(Key key, const Clip& clip) {
    auto* state = new ClipState(root(), clip);
    state->setKey(key);
    addChild(state);
    if (root() != nullptr) root()->states().registerState(state);
    return state;
}

HobiState* HobiLayer::getState(Key& key) {
    for (int guard = 0; guard < 64; ++guard) {
        HobiState* state = nullptr;
        if (root() == nullptr || !root()->states().tryGet(key, state) || state == nullptr) {
            return nullptr;
        }
        if (state->parent() == this) return state;
        if (state->parent() == nullptr) {
            addChild(state);
            return state;
        }
        key = Key::fromPointer(state);
    }
    return nullptr;
}

void HobiLayer::createIfNew(const std::vector<const Clip*>& clips) {
    for (const Clip* clip : clips) {
        if (clip != nullptr) getOrCreateState(*clip);
    }
}

void HobiLayer::createIfNew(std::initializer_list<const Clip*> clips) {
    for (const Clip* clip : clips) {
        if (clip != nullptr) getOrCreateState(*clip);
    }
}

HobiState* HobiLayer::getOrCreateState(const Clip& clip, bool allowSetClip) {
    Key key = Key::fromPointer(&clip);
    if (HobiState* existing = getState(key)) {
        if (existing->clip() != &clip) {
            ANIM_CHECK_MSG(allowSetClip,
                           "the state registered for this key uses a different clip");
            auto* clipState = dynamic_cast<ClipState*>(existing);
            ANIM_CHECK_MSG(clipState != nullptr, "cannot set clip on a non-ClipState");
            clipState->setClip(clip);
        }
        return existing;
    }
    return createState(key, clip);
}

HobiState* HobiLayer::getOrCreateState(ITransition& transition) {
    Key key = transition.key();
    if (HobiState* existing = getState(key)) return existing;

    HobiState* state = transition.createState(*root());
    if (state == nullptr) return nullptr;
    state->setKey(key);
    addChild(state);
    root()->states().registerState(state);
    return state;
}

HobiState* HobiLayer::getOrCreateState(HobiState* state) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");
    if (state->parent() == this) return state;
    if (state->parent() == nullptr) {
        addChild(state);
        return state;
    }

    Key key = state->key().valid() ? state->key() : Key::fromPointer(state);
    if (HobiState* found = getState(key)) return found;

    HobiState* clone = state->clone(root());
    ANIM_CHECK_MSG(clone != nullptr, "state clone returned null");
    clone->setKey(key);
    addChild(clone);
    root()->states().registerState(clone);
    clone->setTimeD(state->timeD());
    clone->setSpeed(state->speed());
    clone->setWeightOnly(state->weight());
    clone->setTargetWeight(state->targetWeight());
    clone->setFadeSpeed(state->fadeSpeed());
    clone->setAutomaticallyClearEvents(state->automaticallyClearEvents());
    if (state->events() != nullptr) clone->setEvents(state->events());
    return clone;
}

HobiState* HobiLayer::getOrCreateWeightlessState(HobiState* state) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");

    if (state->parent() == nullptr) {
        state->setWeightOnly(0.0f);
        state->setTimeD(0.0);
        return state;
    }

    if (state->parent() == this && state->weight() <= weightlessThreshold) {
        state->setTimeD(0.0);
        return state;
    }

    float lowestWeight = std::numeric_limits<float>::infinity();
    HobiState* lowestWeightState = nullptr;
    int cloneCount = 0;

    // 沿 key 链寻找已存在的低权重复本（clone 的 key 是“源 state”的指针）。
    HobiState* chain = state;
    for (int guard = 0; guard < maxCloneCount * 4 + 4; ++guard) {
        HobiState* found = nullptr;
        if (root() == nullptr || !root()->states().tryGet(chain->key(), found)) break;
        if (found == chain) break;

        if (found->parent() == this) {
            if (found->weight() <= weightlessThreshold) {
                found->setTimeD(0.0);
                return found;
            }
            if (lowestWeight > found->weight()) {
                lowestWeight = found->weight();
                lowestWeightState = found;
            }
        } else if (found->parent() == nullptr) {
            addChild(found);
            found->setTimeD(0.0);
            return found;
        }

        chain = found;
        ++cloneCount;
    }

    if (state->parent() == this) {
        lowestWeight = state->weight();
        lowestWeightState = state;
    }

    // 未找到 -> 克隆；超过上限则退化为最低权重的状态。
    HobiState* source = state;
    for (int guard = 0; guard < maxCloneCount * 4 + 4; ++guard) {
        HobiState* found = nullptr;
        const Key cloneKey = Key::fromPointer(source);
        if (root() == nullptr || !root()->states().tryGet(cloneKey, found)) {
            if (cloneCount >= maxCloneCount && lowestWeightState != nullptr) {
                lowestWeightState->setTimeD(0.0);
                return lowestWeightState;
            }

            HobiState* clone = source->clone(root());
            ANIM_CHECK_MSG(clone != nullptr, "state clone returned null");
            clone->setWeightOnly(0.0f);
            clone->setKey(cloneKey);
            root()->states().registerState(clone);
            addChild(clone);
            clone->setTimeD(0.0);
            return clone;
        }

        if (found->parent() == this) {
            if (found->weight() <= weightlessThreshold) {
                found->setTimeD(0.0);
                return found;
            }
            if (lowestWeight > found->weight()) {
                lowestWeight = found->weight();
                lowestWeightState = found;
            }
        } else if (found->parent() == nullptr) {
            addChild(found);
            found->setTimeD(0.0);
            return found;
        }

        source = found;
        ++cloneCount;
    }

    if (lowestWeightState != nullptr) {
        lowestWeightState->setTimeD(0.0);
        return lowestWeightState;
    }
    return state;
}

HobiState* HobiLayer::play(const Clip& clip) { return play(getOrCreateState(clip)); }

HobiState* HobiLayer::play(const Clip& clip, float fadeDuration, FadeMode mode) {
    return play(getOrCreateState(clip), fadeDuration, mode);
}

HobiState* HobiLayer::play(HobiState* state) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");
    state = getOrCreateState(state);
    ANIM_CHECK_MSG(state->parent() == this, "a layer can only play its own states");

    if (weight() == 0.0f && targetWeight() == 0.0f) setWeight(1.0f);

    setCurrentState(state);
    state->play();

    for (HobiState* other : _states) {
        if (other != state) other->stop();
    }

    return state;
}

HobiState* HobiLayer::play(HobiState* state, float fadeDuration, FadeMode mode) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");
    state = getOrCreateState(state);

    if (fadeDuration <= 0.0f ||
        (root() != nullptr && root()->skipFirstFade && index() == 0 && weight() == 0.0f)) {
        setWeight(1.0f);
        state = play(state);
        if (mode == FadeMode::FromStart || mode == FadeMode::NormalizedFromStart) {
            state->setTimeD(0.0);
        }
        return state;
    }

    float layerFadeDuration = fadeDuration;
    evaluateFadeMode(mode, state, fadeDuration, layerFadeDuration);

    startFade(1.0f, layerFadeDuration);
    if (weight() == 0.0f) return play(state);

    if (state->parent() == nullptr) addChild(state);
    ANIM_CHECK_MSG(state->parent() == this, "a layer can only play its own states");

    setCurrentState(state);

    // 已在淡入且会比新淡入更快完成 -> 继续原淡入。
    if (state->isPlaying() && state->targetWeight() == 1.0f &&
        (state->weight() == 1.0f ||
         state->fadeSpeed() * fadeDuration > std::abs(1.0f - state->weight()))) {
        onStartFade();
    } else {
        state->setIsPlaying(true);
        state->startFade(1.0f, fadeDuration);

        for (HobiState* other : _states) {
            if (other != state) other->startFade(0.0f, fadeDuration);
        }
    }

    return state;
}

HobiState* HobiLayer::play(ITransition& transition) {
    return play(transition, transition.fadeDuration(), transition.fadeMode());
}

HobiState* HobiLayer::play(ITransition& transition, float fadeDuration, FadeMode mode) {
    HobiState* state = getOrCreateState(transition);
    if (state == nullptr) return nullptr;
    state = play(state, fadeDuration, mode);
    transition.apply(*state);
    return state;
}

HobiState* HobiLayer::tryPlay(Key key) {
    HobiState* state = nullptr;
    if (root() != nullptr && root()->states().tryGet(key, state)) return play(state);
    return nullptr;
}

void HobiLayer::stop() {
    HobiNode::stop();
    if (_currentState != nullptr) _currentState->stop();
}

void HobiLayer::onStartFade() {
    for (HobiState* state : _states) state->onStartFade();
}

bool HobiLayer::isPlayingClip(const Clip& clip) const {
    for (const HobiState* state : _states) {
        if (state != nullptr && state->clip() == &clip && state->isPlaying()) return true;
    }
    return false;
}

bool HobiLayer::isAnyStatePlaying() const {
    for (const HobiState* state : _states) {
        if (state->isPlaying()) return true;
    }
    return false;
}

float HobiLayer::totalWeight() const {
    float total = 0.0f;
    for (const HobiState* state : _states) total += state->weight();
    return total;
}

void HobiLayer::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    for (const HobiState* state : _states) {
        if (state != nullptr) state->gatherAnimationClips(clips);
    }
}

void HobiLayer::setCurrentState(HobiState* state) {
    _currentState = state;
    ++_commandCount;
}

void HobiLayer::setIsAdditive(bool additive) {
    _additive = additive;
    if (root() == nullptr) return;
    if (auto* mixer = dynamic_cast<LayerMixerNode*>(root()->node(root()->playable()))) {
        mixer->setLayerAdditive(index(), additive);
    }
}

void HobiLayer::setMask(const AvatarMask* mask) {
    _mask = mask;
    if (root() == nullptr) return;
    if (auto* mixer = dynamic_cast<LayerMixerNode*>(root()->node(root()->playable()))) {
        mixer->setLayerMask(index(), mask);
    }
}

void HobiLayer::evaluateFadeMode(FadeMode mode, HobiState*& state, float& fadeDuration,
                                      float& layerFadeDuration) const {
    layerFadeDuration = fadeDuration;

    switch (mode) {
        case FadeMode::FixedSpeed:
            fadeDuration *= std::abs(1.0f - state->weight());
            layerFadeDuration *= std::abs(1.0f - weight());
            break;

        case FadeMode::FixedDuration:
            break;

        case FadeMode::FromStart:
            state = const_cast<HobiLayer*>(this)->getOrCreateWeightlessState(state);
            break;

        case FadeMode::NormalizedSpeed: {
            const float length = state->length();
            fadeDuration *= std::abs(1.0f - state->weight()) * length;
            layerFadeDuration *= std::abs(1.0f - weight()) * length;
            break;
        }

        case FadeMode::NormalizedDuration: {
            const float length = state->length();
            fadeDuration *= length;
            layerFadeDuration *= length;
            break;
        }

        case FadeMode::NormalizedFromStart: {
            state = const_cast<HobiLayer*>(this)->getOrCreateWeightlessState(state);
            const float length = state->length();
            fadeDuration *= length;
            layerFadeDuration *= length;
            break;
        }
    }
}

}  // namespace anim
