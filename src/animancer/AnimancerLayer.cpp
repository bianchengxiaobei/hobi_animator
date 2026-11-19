#include "anim/animancer/AnimancerLayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/ClipState.h"
#include "anim/animancer/StateDictionary.h"
#include "anim/animancer/Transition.h"
#include "anim/core/Assert.h"
#include "anim/graph/Nodes.h"

namespace anim {

float AnimancerLayer::weightlessThreshold = 0.1f;
int AnimancerLayer::maxCloneCount = 3;

AnimancerLayer::AnimancerLayer(AnimancerPlayable* root, int index, PlayableHandle mixerNode)
    : _mixerNode(mixerNode) {
    setRoot(root);
    setIndex(index);
    setPlayable(mixerNode);
    _weight = 1.0f;
    _targetWeight = 1.0f;
    _weightDirty = true;
}

AnimancerState* AnimancerLayer::getChild(int index) const {
    if (index < 0 || index >= childCount()) return nullptr;
    return _states[static_cast<std::size_t>(index)];
}

void AnimancerLayer::addChild(AnimancerState* state) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");
    if (state->parent() == this) return;

    state->setParent(this);
    state->setRoot(root());
    state->setIndex(childCount());
    _states.push_back(state);

    if (graph() != nullptr) graph()->setInputCount(_mixerNode, childCount());
    state->setWeightDirty();
}

void AnimancerLayer::removeChild(AnimancerState* state) {
    auto it = std::find(_states.begin(), _states.end(), state);
    if (it == _states.end()) return;

    const std::ptrdiff_t index = it - _states.begin();
    state->setParent(nullptr);
    state->setIndex(-1);

    // 对齐原项目：用最后一个填充空位，避免整体搬移。
    if (index != static_cast<std::ptrdiff_t>(_states.size()) - 1) {
        AnimancerState* moved = _states.back();
        _states[static_cast<std::size_t>(index)] = moved;
        moved->setIndex(static_cast<int>(index));
    }
    _states.pop_back();

    if (_currentState == state) _currentState = nullptr;
    if (graph() != nullptr) graph()->setInputCount(_mixerNode, childCount());
}

void AnimancerLayer::destroyStates() {
    while (!_states.empty()) {
        _states.back()->destroy();
    }
}

ClipState* AnimancerLayer::createState(const Clip& clip) {
    return createState(Key::fromPointer(&clip), clip);
}

ClipState* AnimancerLayer::createState(Key key, const Clip& clip) {
    auto* state = new ClipState(root(), clip);
    state->setKey(key);
    addChild(state);
    if (root() != nullptr) root()->states().registerState(state);
    return state;
}

AnimancerState* AnimancerLayer::getState(Key& key) {
    for (int guard = 0; guard < 64; ++guard) {
        AnimancerState* state = nullptr;
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

void AnimancerLayer::createIfNew(const std::vector<const Clip*>& clips) {
    for (const Clip* clip : clips) {
        if (clip != nullptr) getOrCreateState(*clip);
    }
}

void AnimancerLayer::createIfNew(std::initializer_list<const Clip*> clips) {
    for (const Clip* clip : clips) {
        if (clip != nullptr) getOrCreateState(*clip);
    }
}

AnimancerState* AnimancerLayer::getOrCreateState(const Clip& clip, bool allowSetClip) {
    Key key = Key::fromPointer(&clip);
    if (AnimancerState* existing = getState(key)) {
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

AnimancerState* AnimancerLayer::getOrCreateState(ITransition& transition) {
    Key key = transition.key();
    if (AnimancerState* existing = getState(key)) return existing;

    AnimancerState* state = transition.createState(*root());
    if (state == nullptr) return nullptr;
    state->setKey(key);
    addChild(state);
    root()->states().registerState(state);
    return state;
}

AnimancerState* AnimancerLayer::getOrCreateState(AnimancerState* state) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");
    if (state->parent() == this) return state;
    if (state->parent() == nullptr) {
        addChild(state);
        return state;
    }

    Key key = state->key().valid() ? state->key() : Key::fromPointer(state);
    if (AnimancerState* found = getState(key)) return found;

    AnimancerState* clone = state->clone(root());
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

AnimancerState* AnimancerLayer::getOrCreateWeightlessState(AnimancerState* state) {
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
    AnimancerState* lowestWeightState = nullptr;
    int cloneCount = 0;

    // 沿 key 链寻找已存在的低权重复本（clone 的 key 是“源 state”的指针）。
    AnimancerState* chain = state;
    for (int guard = 0; guard < maxCloneCount * 4 + 4; ++guard) {
        AnimancerState* found = nullptr;
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
    AnimancerState* source = state;
    for (int guard = 0; guard < maxCloneCount * 4 + 4; ++guard) {
        AnimancerState* found = nullptr;
        const Key cloneKey = Key::fromPointer(source);
        if (root() == nullptr || !root()->states().tryGet(cloneKey, found)) {
            if (cloneCount >= maxCloneCount && lowestWeightState != nullptr) {
                lowestWeightState->setTimeD(0.0);
                return lowestWeightState;
            }

            AnimancerState* clone = source->clone(root());
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

AnimancerState* AnimancerLayer::play(const Clip& clip) { return play(getOrCreateState(clip)); }

AnimancerState* AnimancerLayer::play(const Clip& clip, float fadeDuration, FadeMode mode) {
    return play(getOrCreateState(clip), fadeDuration, mode);
}

AnimancerState* AnimancerLayer::play(AnimancerState* state) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");
    state = getOrCreateState(state);
    ANIM_CHECK_MSG(state->parent() == this, "a layer can only play its own states");

    if (weight() == 0.0f && targetWeight() == 0.0f) setWeight(1.0f);

    setCurrentState(state);
    state->play();

    for (AnimancerState* other : _states) {
        if (other != state) other->stop();
    }

    return state;
}

AnimancerState* AnimancerLayer::play(AnimancerState* state, float fadeDuration, FadeMode mode) {
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

        for (AnimancerState* other : _states) {
            if (other != state) other->startFade(0.0f, fadeDuration);
        }
    }

    return state;
}

AnimancerState* AnimancerLayer::play(ITransition& transition) {
    return play(transition, transition.fadeDuration(), transition.fadeMode());
}

AnimancerState* AnimancerLayer::play(ITransition& transition, float fadeDuration, FadeMode mode) {
    AnimancerState* state = getOrCreateState(transition);
    if (state == nullptr) return nullptr;
    state = play(state, fadeDuration, mode);
    transition.apply(*state);
    return state;
}

AnimancerState* AnimancerLayer::tryPlay(Key key) {
    AnimancerState* state = nullptr;
    if (root() != nullptr && root()->states().tryGet(key, state)) return play(state);
    return nullptr;
}

void AnimancerLayer::stop() {
    AnimancerNode::stop();
    if (_currentState != nullptr) _currentState->stop();
}

void AnimancerLayer::onStartFade() {
    for (AnimancerState* state : _states) state->onStartFade();
}

bool AnimancerLayer::isPlayingClip(const Clip& clip) const {
    for (const AnimancerState* state : _states) {
        if (state != nullptr && state->clip() == &clip && state->isPlaying()) return true;
    }
    return false;
}

bool AnimancerLayer::isAnyStatePlaying() const {
    for (const AnimancerState* state : _states) {
        if (state->isPlaying()) return true;
    }
    return false;
}

float AnimancerLayer::totalWeight() const {
    float total = 0.0f;
    for (const AnimancerState* state : _states) total += state->weight();
    return total;
}

void AnimancerLayer::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    for (const AnimancerState* state : _states) {
        if (state != nullptr) state->gatherAnimationClips(clips);
    }
}

void AnimancerLayer::setCurrentState(AnimancerState* state) {
    _currentState = state;
    ++_commandCount;
}

void AnimancerLayer::setIsAdditive(bool additive) {
    _additive = additive;
    if (root() == nullptr) return;
    if (auto* mixer = dynamic_cast<LayerMixerNode*>(root()->node(root()->playable()))) {
        mixer->setLayerAdditive(index(), additive);
    }
}

void AnimancerLayer::setMask(const AvatarMask* mask) {
    _mask = mask;
    if (root() == nullptr) return;
    if (auto* mixer = dynamic_cast<LayerMixerNode*>(root()->node(root()->playable()))) {
        mixer->setLayerMask(index(), mask);
    }
}

void AnimancerLayer::evaluateFadeMode(FadeMode mode, AnimancerState*& state, float& fadeDuration,
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
            state = const_cast<AnimancerLayer*>(this)->getOrCreateWeightlessState(state);
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
            state = const_cast<AnimancerLayer*>(this)->getOrCreateWeightlessState(state);
            const float length = state->length();
            fadeDuration *= length;
            layerFadeDuration *= length;
            break;
        }
    }
}

}  // namespace anim
