#pragma once

#include <vector>

#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/clip/Clip.h"
#include "anim/core/Assert.h"
#include "anim/core/Math.h"

// 混合器状态（对应 ManualMixerState / LinearMixerState / Directional / Cartesian）。
//
// 文档：docs/08-混合器.md
// 对齐 Internal/Mixer States：
//  - ManualMixerState.cs：子状态管理、ChildCapacity、NormalizeWeights(:1319)、
//    Synchronize(:919)、ApplySynchronizeChildren(:1116)、Update(:812)
//  - LinearMixerState.cs:155-222（线性插值 + ExtrapolateSpeed）
//  - DirectionalMixerState.cs:76-226（Polar Gradient Band Interpolation）
//  - CartesianMixerState.cs:70-165（Gradient Band Interpolation）
namespace anim {

class AnimancerPlayable;
class ClipState;
class ITransition;

// 手动控制每个子状态权重的混合器。
class ManualMixerState : public AnimancerState {
public:
    explicit ManualMixerState(AnimancerPlayable* root = nullptr);
    ~ManualMixerState() override;

    // 混合器始终保留子节点连接（ManualMixerState.cs:45）。
    bool keepChildrenConnected() const noexcept override { return true; }

    const Clip* clip() const noexcept override { return nullptr; }
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

    int childCount() const noexcept override { return _childCount; }
    AnimancerState* getChild(int index) const override;

    int childCapacity() const noexcept { return static_cast<int>(_children.size()); }
    void setChildCapacity(int value);
    void ensureRemainingChildCapacity(int minimumCapacity);

    // ---- 子状态配置 ----
    void add(AnimancerState* state);
    ClipState* add(const Clip& clip);
    AnimancerState* add(ITransition& transition);
    void addRange(const std::vector<const Clip*>& clips);

    void remove(int index, bool destroy);
    void remove(AnimancerState* child, bool destroy);
    void set(int index, AnimancerState* child, bool destroyPrevious);
    ClipState* set(int index, const Clip& clip, bool destroyPrevious);
    int indexOf(const AnimancerState* child) const;

    void destroyChildren();

    // ---- 时长/时间 ----
    float length() const override;
    bool isLooping() const noexcept override;
    double timeD() const noexcept override;
    void setTimeD(double value) override;
    void moveTime(double time, bool normalized) override;

    // ---- 权重 ----
    bool weightsAreDirty() const noexcept { return _weightsAreDirty; }
    void setWeightsAreDirty(bool value) noexcept {
        _weightsAreDirty = value;
        if (value && root() != nullptr) root()->requirePreUpdate(this);
    }
    // 返回是否真的重算过（对齐 ManualMixerState.cs:837-848）。
    bool recalculateWeights();

    void normalizeWeights(float totalWeight);
    static float calculateTotalWeight(const AnimancerState* const* states, int count);

    // ---- 同步 ----
    bool isSynchronized(const AnimancerState* state) const;
    void synchronize(AnimancerState* state);
    void dontSynchronize(AnimancerState* state);
    void dontSynchronizeChildren();
    void initializeSynchronizedChildren(const std::vector<bool>& flags);
    int synchronizedChildCount() const noexcept;
    ManualMixerState* getParentMixer();

    // ---- 生命周期 ----
    void createPlayable() override;
    void update(bool& needsMoreUpdates) override;
    void destroy() override;
    AnimancerState* clone(AnimancerPlayable* root) override;

    static int defaultChildCapacity;              // 默认 8
    static bool synchronizeNewChildren;           // 默认 true
    static float minimumSynchronizeChildrenWeight;  // 默认 0.01

protected:
    void onSetIsPlaying() override;

    // 子类在此计算并设置子状态权重（必须清除 _weightsAreDirty）。
    virtual void forceRecalculateWeights() { _weightsAreDirty = false; }
    virtual void onChildCapacityChanged() {}

    void disableRemainingStates(int previousIndex);
    void connectChildren();
    void onAddChild(AnimancerState* state);
    void onRemoveChild(AnimancerState* state);

    void applySynchronizeChildren(bool& needsMoreUpdates);
    float calculateRealEffectiveSpeed() const;
    void copyStructureTo(ManualMixerState* clone) const;

    std::vector<AnimancerState*> _children;
    int _childCount = 0;
    bool _weightsAreDirty = false;
    std::vector<AnimancerState*> _synchronizedChildren;
};

// 参数化混合器基类（对应 MixerState<TParameter>）。
template <class TParameter>
class MixerState : public ManualMixerState {
public:
    explicit MixerState(AnimancerPlayable* root = nullptr) : ManualMixerState(root) {}

    const TParameter& parameter() const noexcept { return _parameter; }
    void setParameter(const TParameter& value) {
        _parameter = value;
        _weightsAreDirty = true;
        if (root() != nullptr) root()->requirePreUpdate(this);
    }

    bool hasThresholds() const noexcept {
        return static_cast<int>(_thresholds.size()) >= childCount();
    }

    const TParameter& getThreshold(int index) const { return _thresholds[static_cast<std::size_t>(index)]; }

    void setThreshold(int index, const TParameter& threshold) {
        if (static_cast<int>(_thresholds.size()) <= index)
            _thresholds.resize(static_cast<std::size_t>(index) + 1);
        _thresholds[static_cast<std::size_t>(index)] = threshold;
        onThresholdsChanged();
    }

    void setThresholds(const std::vector<TParameter>& thresholds) {
        ANIM_CHECK_MSG(static_cast<int>(thresholds.size()) >= childCount(),
                       "Threshold count must not be less than child count");
        _thresholds = thresholds;
        onThresholdsChanged();
    }

    void validateThresholdCount() {
        if (static_cast<int>(_thresholds.size()) < childCount())
            _thresholds.resize(static_cast<std::size_t>(childCapacity()));
    }

    virtual void onThresholdsChanged() {
        _weightsAreDirty = true;
        if (root() != nullptr) root()->requirePreUpdate(this);
    }

protected:
    void onChildCapacityChanged() override {
        _thresholds.resize(static_cast<std::size_t>(childCapacity()));
        onThresholdsChanged();
    }

    TParameter _parameter{};
    std::vector<TParameter> _thresholds;
};

// 一维线性混合（1D Blend Tree 等价物）。
class LinearMixerState : public MixerState<float> {
public:
    explicit LinearMixerState(AnimancerPlayable* root = nullptr) : MixerState<float>(root) {}

    bool extrapolateSpeed() const noexcept { return _extrapolateSpeed; }
    void setExtrapolateSpeed(bool value);

    void onThresholdsChanged() override;

    void assertThresholdsSorted();
    LinearMixerState& assignLinearThresholds(float min = 0.0f, float max = 1.0f);

    AnimancerState* clone(AnimancerPlayable* root) override;

protected:
    void forceRecalculateWeights() override;

private:
    bool _extrapolateSpeed = true;
    bool _needToCheckThresholdSorting = false;
};

// 二维混合：极坐标梯度带插值（2D Freeform Directional 等价物）。
class DirectionalMixerState : public MixerState<Vec2> {
public:
    explicit DirectionalMixerState(AnimancerPlayable* root = nullptr) : MixerState<Vec2>(root) {}

    float parameterX() const noexcept { return _parameter.x; }
    void setParameterX(float value) { setParameter(Vec2(value, _parameter.y)); }
    float parameterY() const noexcept { return _parameter.y; }
    void setParameterY(float value) { setParameter(Vec2(_parameter.x, value)); }

    void onThresholdsChanged() override;

    AnimancerState* clone(AnimancerPlayable* root) override;

protected:
    void forceRecalculateWeights() override;

private:
    void calculateBlendFactors(int childCount);

    std::vector<float> _thresholdMagnitudes;
    std::vector<std::vector<Vec2>> _blendFactors;
    bool _blendFactorsDirty = true;

    static constexpr float angleFactor = 2.0f;
};

// 二维混合：笛卡尔梯度带插值（2D Freeform Cartesian 等价物）。
class CartesianMixerState : public MixerState<Vec2> {
public:
    explicit CartesianMixerState(AnimancerPlayable* root = nullptr) : MixerState<Vec2>(root) {}

    float parameterX() const noexcept { return _parameter.x; }
    void setParameterX(float value) { setParameter(Vec2(value, _parameter.y)); }
    float parameterY() const noexcept { return _parameter.y; }
    void setParameterY(float value) { setParameter(Vec2(_parameter.x, value)); }

    void onThresholdsChanged() override;

    AnimancerState* clone(AnimancerPlayable* root) override;

protected:
    void forceRecalculateWeights() override;

private:
    void calculateBlendFactors(int childCount);

    std::vector<std::vector<Vec2>> _blendFactors;
    bool _blendFactorsDirty = true;
};

}  // namespace anim
