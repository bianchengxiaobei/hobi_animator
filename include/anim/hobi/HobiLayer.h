#pragma once

#include <initializer_list>
#include <vector>

#include "anim/hobi/AnimationClipCollection.h"
#include "anim/hobi/HobiNode.h"
#include "anim/hobi/Key.h"
#include "anim/skeleton/AvatarMask.h"

// 动画层（对应 HobiLayer）。
//
// 文档：docs/07-Hobi门面.md §4
// 对齐 Internal/Core/AnimancerLayer.cs：
//  - Play 立即/交叉淡入（:656-783）
//  - EvaluateFadeMode（:859-906）
//  - Weightless 克隆（:491-616）
namespace anim {

class HobiPlayable;
class HobiState;
struct Clip;
class ClipState;
class ITransition;
struct Key;

class HobiLayer final : public HobiNode, public IAnimationClipCollection {
public:
    HobiLayer(HobiPlayable* root, int index, PlayableHandle mixerNode);

    HobiLayer* layer() const noexcept override { return const_cast<HobiLayer*>(this); }
    bool keepChildrenConnected() const noexcept override { return true; }

    HobiState* currentState() const noexcept { return _currentState; }
    int commandCount() const noexcept { return _commandCount; }

    bool isAdditive() const noexcept { return _additive; }
    void setIsAdditive(bool additive);
    void setMask(const AvatarMask* mask);
    const AvatarMask* mask() const noexcept { return _mask; }

    int childCount() const noexcept override { return static_cast<int>(_states.size()); }
    HobiState* getChild(int index) const override;

    void addChild(HobiState* state);
    void removeChild(HobiState* state);
    void destroyStates();

    ClipState* createState(const Clip& clip);
    void createIfNew(const std::vector<const Clip*>& clips);
    void createIfNew(std::initializer_list<const Clip*> clips);
    HobiState* getState(Key& key);
    HobiState* getOrCreateState(const Clip& clip, bool allowSetClip = false);
    HobiState* getOrCreateState(ITransition& transition);
    HobiState* getOrCreateState(HobiState* state);
    HobiState* getOrCreateWeightlessState(HobiState* state);

    HobiState* play(const Clip& clip);
    HobiState* play(const Clip& clip, float fadeDuration, FadeMode mode);
    HobiState* play(HobiState* state);
    HobiState* play(HobiState* state, float fadeDuration, FadeMode mode);
    HobiState* play(ITransition& transition);
    HobiState* play(ITransition& transition, float fadeDuration, FadeMode mode);
    HobiState* tryPlay(Key key);

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
    void setCurrentState(HobiState* state);
    void evaluateFadeMode(FadeMode mode, HobiState*& state, float& fadeDuration,
                          float& layerFadeDuration) const;

    PlayableHandle _mixerNode{};
    std::vector<HobiState*> _states;
    HobiState* _currentState = nullptr;
    int _commandCount = 0;
    bool _additive = false;
    const AvatarMask* _mask = nullptr;
};

}  // namespace anim
