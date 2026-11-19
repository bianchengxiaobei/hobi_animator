#pragma once

#include <cmath>
#include <limits>

#include "anim/hobi/AnimationClipCollection.h"
#include "anim/hobi/Event.h"
#include "anim/hobi/FadeMode.h"
#include "anim/hobi/Key.h"
#include "anim/clip/Clip.h"

// 过渡（对应 ITransition / AnimancerTransition / ClipTransition）。
//
// 文档：docs/07-Hobi门面.md §7
namespace anim {

class HobiPlayable;
class HobiState;
class Skeleton;
struct HumanoidAvatar;

class ITransition : public IAnimationClipCollection {
public:
    virtual ~ITransition() = default;

    virtual HobiState* createState(HobiPlayable& root) = 0;
    virtual void apply(HobiState& state) = 0;

    virtual float fadeDuration() const = 0;
    virtual FadeMode fadeMode() const = 0;

    virtual Key key() const { return Key::fromPointer(this); }
    virtual EventSequence* events() { return nullptr; }
    virtual bool isValid() const { return true; }
    void gatherAnimationClips(std::vector<const Clip*>&) const override {}
};

class ClipTransition : public ITransition {
public:
    const Clip* clip = nullptr;
    float fadeDurationValue = 0.25f;
    float speedValue = 1.0f;
    float normalizedStartTime = std::numeric_limits<float>::quiet_NaN();
    EventSequence eventSequence;

    HobiState* createState(HobiPlayable& root) override;

    void apply(HobiState& state) override;

    float fadeDuration() const override { return fadeDurationValue; }

    FadeMode fadeMode() const override {
        return std::isnan(normalizedStartTime) ? FadeMode::FixedSpeed : FadeMode::FromStart;
    }

    EventSequence* events() override { return &eventSequence; }
    bool isValid() const override { return clip != nullptr; }

    Key key() const override { return Key::fromPointer(clip); }

    virtual float length() const;
    virtual float maximumDuration() const;
    virtual bool isLooping() const noexcept;
    virtual float averageAngularSpeed() const;
    virtual Vec3 averageVelocity() const;
    virtual void gatherClips(std::vector<const Clip*>& clips) const;
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;
    virtual void copyFrom(const ClipTransition& other);
};

class RetargetClipTransition : public ITransition {
public:
    const Clip* clip = nullptr;
    const Skeleton* sourceSkeleton = nullptr;
    const HumanoidAvatar* sourceAvatar = nullptr;
    const HumanoidAvatar* targetAvatar = nullptr;
    float fadeDurationValue = 0.25f;
    float speedValue = 1.0f;
    float normalizedStartTime = std::numeric_limits<float>::quiet_NaN();
    EventSequence eventSequence;

    HobiState* createState(HobiPlayable& root) override;
    void apply(HobiState& state) override;

    float fadeDuration() const override { return fadeDurationValue; }

    FadeMode fadeMode() const override {
        return std::isnan(normalizedStartTime) ? FadeMode::FixedSpeed : FadeMode::FromStart;
    }

    EventSequence* events() override { return &eventSequence; }
    bool isValid() const override {
        return clip != nullptr && clip->type == ClipType::BoneLocal &&
               sourceSkeleton != nullptr && sourceAvatar != nullptr &&
               targetAvatar != nullptr;
    }
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override {
        gatherClip(clips, clip);
    }
};

// 可复用配置资源（对应 TransitionAsset）。共享事件序列由调用方决定；
// 需要每对象独立事件时用 UnShared 拷贝一份。
template <class TTransition>
class TransitionAsset {
public:
    TTransition transition;

    HobiState* createState(HobiPlayable& root) { return transition.createState(root); }
    void apply(HobiState& state) { transition.apply(state); }
    void gatherAnimationClips(std::vector<const Clip*>& clips) const {
        transition.gatherAnimationClips(clips);
    }
    TTransition& get() noexcept { return transition; }
    const TTransition& get() const noexcept { return transition; }
};

// 每个实例持有一份配置/事件副本（对应 UnShared<TAsset>）。
template <class TAsset>
class UnShared {
public:
    UnShared() = default;
    explicit UnShared(const TAsset& asset) : _base(asset) {}

    TAsset& base() noexcept { return _base; }
    const TAsset& base() const noexcept { return _base; }

    HobiState* createState(HobiPlayable& root) { return _base.createState(root); }
    void apply(HobiState& state) { _base.apply(state); }
    void gatherAnimationClips(std::vector<const Clip*>& clips) const {
        _base.gatherAnimationClips(clips);
    }

private:
    TAsset _base;
};

}  // namespace anim
