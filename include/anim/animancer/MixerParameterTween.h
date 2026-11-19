#pragma once

#include "anim/animancer/MixerStates.h"
#include "anim/core/Assert.h"

// 混合器参数平滑（对应 Utilities/Custom Fade/MixerParameterTween.cs）。
//
// 调用方持有 tween 对象；start() 把对象注册到 Playable 的 pre-update 列表，
// stop() 或到达目标后自动取消。pre-update 期间不会取得所有权。
namespace anim {

template <class TParameter>
class MixerParameterTween : public IUpdatable {
public:
    MixerState<TParameter>* mixer = nullptr;

    TParameter startValue{};
    TParameter endValue{};
    float duration = 0.0f;
    float time = 0.0f;

    MixerParameterTween() = default;
    explicit MixerParameterTween(MixerState<TParameter>* target) : mixer(target) {}

    float progress() const { return time / duration; }
    void setProgress(float value) { time = value * duration; }

    void start(const TParameter& targetValue, float targetDuration) {
        ANIM_CHECK_MSG(mixer != nullptr, "Mixer is null");
        AnimancerPlayable* root = mixer->root();
        ANIM_CHECK_MSG(root != nullptr, "Mixer is not connected to an AnimancerPlayable");

        startValue = mixer->parameter();
        endValue = targetValue;
        duration = targetDuration;
        time = 0.0f;
        root->requirePreUpdate(this);
    }

    void stop() {
        if (mixer != nullptr && mixer->root() != nullptr)
            mixer->root()->cancelPreUpdate(this);
    }

    bool isActive() const {
        return mixer != nullptr && mixer->root() != nullptr &&
               mixer->root()->isPreUpdating(this);
    }

    void update(bool& needsMoreUpdates) override {
        needsMoreUpdates = false;

        if (mixer == nullptr || mixer->root() == nullptr) {
            stop();
            return;
        }

        time += mixer->root()->deltaTime();

        if (time < duration) {
            mixer->setParameter(calculateCurrentValue());
            return;
        }

        time = duration;
        mixer->setParameter(endValue);
        stop();
    }

    bool requiresContinuousUpdate() const noexcept override { return true; }

protected:
    virtual TParameter calculateCurrentValue() = 0;
};

class MixerParameterTweenFloat final : public MixerParameterTween<float> {
public:
    MixerParameterTweenFloat() = default;
    explicit MixerParameterTweenFloat(MixerState<float>* mixer)
        : MixerParameterTween<float>(mixer) {}

protected:
    float calculateCurrentValue() override {
        return startValue + (endValue - startValue) * progress();
    }
};

class MixerParameterTweenVector2 final : public MixerParameterTween<Vec2> {
public:
    MixerParameterTweenVector2() = default;
    explicit MixerParameterTweenVector2(MixerState<Vec2>* mixer)
        : MixerParameterTween<Vec2>(mixer) {}

protected:
    Vec2 calculateCurrentValue() override {
        return startValue + (endValue - startValue) * progress();
    }
};

using MixerParameterTweenVec2 = MixerParameterTweenVector2;

}  // namespace anim
