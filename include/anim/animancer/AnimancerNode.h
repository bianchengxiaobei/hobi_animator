#pragma once

#include <cstdint>

#include "anim/animancer/FadeMode.h"
#include "anim/core/IUpdatable.h"
#include "anim/graph/PlayableHandle.h"

// Animancer 节点基类：Weight / Fade / Speed / IK / 连接策略。
//
// 文档：docs/07-Animancer门面.md §2
// 逐条对齐 Internal/Core/AnimancerNode.cs：
//  - Weight setter 同时清 TargetWeight/FadeSpeed (:514-523)
//  - SetWeight 保留淡入淡出 (:534-546)
//  - StartFade (:657-689)
//  - UpdateFade (:713-762)
//  - ApplyWeight (:560-588)
namespace anim {

class AnimancerPlayable;
class AnimancerLayer;
class AnimancerState;
class PlayableGraph;

class AnimancerNode : public IUpdatable {
public:
    ~AnimancerNode() override;

    AnimancerPlayable* root() const noexcept { return _root; }
    void setRoot(AnimancerPlayable* root) noexcept { _root = root; }

    AnimancerNode* parent() const noexcept { return _parent; }
    void setParent(AnimancerNode* parent) noexcept { _parent = parent; }

    virtual AnimancerLayer* layer() const noexcept { return nullptr; }
    virtual int childCount() const noexcept { return 0; }
    virtual AnimancerState* getChild(int /*index*/) const { return nullptr; }
    virtual bool keepChildrenConnected() const noexcept { return false; }

    PlayableHandle playable() const noexcept { return _playable; }
    void setPlayable(PlayableHandle handle) noexcept { _playable = handle; }

    int index() const noexcept { return _index; }
    void setIndex(int index) noexcept { _index = index; }

    // ---- 权重 ----
    float weight() const noexcept { return _weight; }
    // 同时清 TargetWeight/FadeSpeed（对齐 Weight setter）。
    void setWeight(float value);

    // 只改当前权重并标脏，保留正在进行的淡入淡出。
    void setWeightOnly(float value);

    float targetWeight() const noexcept { return _targetWeight; }
    void setTargetWeight(float value) noexcept { _targetWeight = value; }

    float fadeSpeed() const noexcept { return _fadeSpeed; }
    void setFadeSpeed(float value) noexcept { _fadeSpeed = value; }

    float effectiveWeight() const noexcept;

    void applyWeight();

    void startFade(float targetWeight);
    void startFade(float targetWeight, float duration);

    virtual void stop();

    // ---- 速度 ----
    float speed() const noexcept { return _speed; }
    void setSpeed(float value);

    float effectiveSpeed() const noexcept;
    // 反推所需 Speed（用于 Duration/RemainingDuration 的 setter）。
    void setEffectiveSpeed(float value);

    // 图节点速度倍率（对应 LinearMixerState.ExtrapolateSpeed 修改 Playable 速度）。
    // 不改变 Speed 属性，但会参与自己和子状态的 EffectiveSpeed。
    float playbackSpeedScale() const noexcept { return _playbackSpeedScale; }
    void setPlaybackSpeedScale(float value) { _playbackSpeedScale = value; }

    // ---- IK ----
    bool applyAnimatorIK() const noexcept { return _applyAnimatorIK; }
    bool applyFootIK() const noexcept { return _applyFootIK; }
    void setApplyAnimatorIK(bool value) noexcept { _applyAnimatorIK = value; }
    void setApplyFootIK(bool value) noexcept { _applyFootIK = value; }

    // ---- 图连接 ----
    virtual void createPlayable();
    void destroyPlayable();
    void connectToGraph();
    void disconnectFromGraph();

    // 由 pre-update 驱动。
    void update(bool& needsMoreUpdates) override;

protected:
    friend class AnimancerLayer;
    friend class AnimancerState;

    virtual void onStartFade() {}

    virtual void setWeightDirty() noexcept;
    void updateFade(bool& needsMoreUpdates);

    PlayableGraph* graph() const noexcept;

    AnimancerPlayable* _root = nullptr;
    AnimancerNode* _parent = nullptr;
    PlayableHandle _playable{};
    int _index = -1;

    float _weight = 0.0f;
    float _targetWeight = 0.0f;
    float _fadeSpeed = 0.0f;
    float _speed = 1.0f;
    float _playbackSpeedScale = 1.0f;

    bool _weightDirty = true;
    bool _connected = false;
    bool _applyAnimatorIK = true;
    bool _applyFootIK = true;
};

}  // namespace anim
