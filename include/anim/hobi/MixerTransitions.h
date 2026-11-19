#pragma once

#include <vector>

#include "anim/hobi/Transition.h"
#include "anim/core/Math.h"

// 混合器过渡（对应 ManualMixerTransition / LinearMixerTransition / MixerTransition2D）。
//
// 文档：docs/08-混合器.md
// 对齐 Utilities/Transitions：
//  - ManualMixerTransitionAsset.cs:219-251（InitializeState：AddRange + Speeds + Sync）
//  - ManualMixerTransitionAsset.cs:255-266（Apply：Speed + 子 Transition）
//  - MixerTransition.cs:51-58（InitializeState：Thresholds + DefaultParameter）
//  - MixerTransition2DAsset.cs:53-76（按 Type 创建 Cartesian/Directional）
namespace anim {

class HobiPlayable;
class HobiState;
class ManualMixerState;

class ManualMixerTransition : public ITransition {
public:
    std::vector<const Clip*> clips;
    std::vector<float> speeds;               // 可为空；长度不足的部分忽略
    std::vector<bool> synchronizeChildren;   // 可为空；缺省视为 true

    float fadeDurationValue = 0.25f;
    float speedValue = 1.0f;
    EventSequence eventSequence;

    float fadeDuration() const override { return fadeDurationValue; }
    FadeMode fadeMode() const override { return FadeMode::FixedSpeed; }
    EventSequence* events() override { return &eventSequence; }

    bool isValid() const override {
        if (clips.empty()) return false;
        for (const Clip* clip : clips) {
            if (clip == nullptr) return false;
        }
        return true;
    }

    Key key() const override { return Key::fromPointer(this); }

    HobiState* createState(HobiPlayable& root) override;
    void apply(HobiState& state) override;
    void gatherAnimationClips(std::vector<const Clip*>& output) const override;

protected:
    virtual ManualMixerState* createMixer(HobiPlayable& root) const;
    virtual void initializeState(ManualMixerState& mixer) const;
};

class LinearMixerTransition : public ManualMixerTransition {
public:
    std::vector<float> thresholds;
    float defaultParameter = 0.0f;
    bool extrapolateSpeed = true;

    Key key() const override { return Key::fromPointer(this); }

protected:
    ManualMixerState* createMixer(HobiPlayable& root) const override;
    void initializeState(ManualMixerState& mixer) const override;
};

class MixerTransition2D : public ManualMixerTransition {
public:
    enum class Type { Cartesian, Directional };

    Type type = Type::Cartesian;
    std::vector<Vec2> thresholds;
    Vec2 defaultParameter{0.0f, 0.0f};

    Key key() const override { return Key::fromPointer(this); }

protected:
    ManualMixerState* createMixer(HobiPlayable& root) const override;
    void initializeState(ManualMixerState& mixer) const override;
};

}  // namespace anim
