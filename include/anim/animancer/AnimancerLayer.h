#pragma once

#include <initializer_list>
#include <vector>

#include "anim/animancer/AnimationClipCollection.h"
#include "anim/animancer/AnimancerNode.h"
#include "anim/animancer/Key.h"
#include "anim/skeleton/AvatarMask.h"

// 动画层（对应 AnimancerLayer）。
//
// 文档：docs/07-Animancer门面.md §4
// 对齐 Internal/Core/AnimancerLayer.cs：
//  - Play 立即/交叉淡入（:656-783）
//  - EvaluateFadeMode（:859-906）
//  - Weightless 克隆（:491-616）
namespace anim {

class AnimancerPlayable;
class AnimancerState;
struct Clip;
class ClipState;
class ITransition;
struct Key;

class AnimancerLayer final : public AnimancerNode, public IAnimationClipCollection {
public:
    AnimancerLayer(AnimancerPlayable* root, int index, PlayableHandle mixerNode);

    AnimancerLayer* layer() const noexcept override { return const_cast<AnimancerLayer*>(this); }
    bool keepChildrenConnected() const noexcept override { return true; }

    AnimancerState* currentState() const noexcept { return _currentState; }
    int commandCount() const noexcept { return _commandCount; }

    bool isAdditive() const noexcept { return _additive; }
    void setIsAdditive(bool additive);
    void setMask(const AvatarMask* mask);
    const AvatarMask* mask() const noexcept { return _mask; }

    int childCount() const noexcept override { return static_cast<int>(_states.size()); }
    AnimancerState* getChild(int index) const override;

    void addChild(AnimancerState* state);
    void removeChild(AnimancerState* state);
    void destroyStates();

    ClipState* createState(const Clip& clip);
    void createIfNew(const std::vector<const Clip*>& clips);
    void createIfNew(std::initializer_list<const Clip*> clips);
    AnimancerState* getState(Key& key);
    AnimancerState* getOrCreateState(const Clip& clip, bool allowSetClip = false);
    AnimancerState* getOrCreateState(ITransition& transition);
    AnimancerState* getOrCreateState(AnimancerState* state);
    AnimancerState* getOrCreateWeightlessState(AnimancerState* state);

    AnimancerState* play(const Clip& clip);
    AnimancerState* play(const Clip& clip, float fadeDuration, FadeMode mode);
    AnimancerState* play(AnimancerState* state);
    AnimancerState* play(AnimancerState* state, float fadeDuration, FadeMode mode);
    AnimancerState* play(ITransition& transition);
    AnimancerState* play(ITransition& transition, float fadeDuration, FadeMode mode);
    AnimancerState* tryPlay(Key key);

    void stop() override;
    void onStartFade() override;

    bool isPlayingClip(const Clip& clip) const;
    bool isAnyStatePlaying() const;
    float totalWeight() const;
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

    static float weightlessThreshold;  // 默认 0.1
    static int maxCloneCount;          // 默认 3

private:
    ClipState* createState(Key key, const Clip& clip);
    void setCurrentState(AnimancerState* state);
    void evaluateFadeMode(FadeMode mode, AnimancerState*& state, float& fadeDuration,
                          float& layerFadeDuration) const;

    PlayableHandle _mixerNode{};
    std::vector<AnimancerState*> _states;
    AnimancerState* _currentState = nullptr;
    int _commandCount = 0;
    bool _additive = false;
    const AvatarMask* _mask = nullptr;
};

}  // namespace anim
