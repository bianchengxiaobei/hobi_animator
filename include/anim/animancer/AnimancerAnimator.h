#pragma once

#include <memory>
#include <vector>

#include "anim/animancer/AnimationClipCollection.h"
#include "anim/animancer/FadeMode.h"
#include "anim/animancer/Key.h"
#include "anim/core/Math.h"
#include "anim/skeleton/RootMotion.h"

// 组件等价物（对应 AnimancerComponent 的核心职责）。
//
// 文档：docs/07-Animancer门面.md §10
namespace anim {

class AnimancerLayer;
class AnimancerPlayable;
class AnimancerState;
struct Clip;
struct HumanoidAvatar;
class ITransition;
class Skeleton;

class AnimancerAnimator : public IAnimationClipCollection {
public:
    AnimancerAnimator();
    ~AnimancerAnimator();

    AnimancerAnimator(const AnimancerAnimator&) = delete;
    AnimancerAnimator& operator=(const AnimancerAnimator&) = delete;

    void initialize(Skeleton& skeleton, const HumanoidAvatar* avatar = nullptr,
                    int layerCount = 1);
    void destroy();
    bool isInitialized() const noexcept { return _playable != nullptr; }

    // 推进动画并更新调色板。
    void update(float deltaTime);
    void evaluate(float deltaTime);
    void evaluate();

    float speed() const noexcept;
    void setSpeed(float value);
    bool isGraphPlaying() const noexcept;
    void pauseGraph();
    void unpauseGraph();

    AnimancerPlayable& playable() { return *_playable; }
    const AnimancerPlayable& playable() const { return *_playable; }
    Skeleton* skeleton() const noexcept { return _skeleton; }

    const std::vector<Mat4>& palette() const noexcept { return _palette; }

    // 由外部决定是否消费；本库不自动修改任何 Transform（对应原项目
    // RedirectRootMotionToTransform 的可选性）。
    RootMotionDelta consumeRootMotion() const;

    AnimancerState* play(const Clip& clip);
    AnimancerState* play(const Clip& clip, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    AnimancerState* play(ITransition& transition);
    AnimancerState* play(ITransition& transition, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    AnimancerState* play(AnimancerState* state);
    AnimancerState* play(AnimancerState* state, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    AnimancerState* tryPlay(Key key);
    AnimancerState* tryPlay(Key key, float fadeDuration,
                            FadeMode mode = FadeMode::FixedSpeed);
    void stop();
    AnimancerState* stop(const Clip& clip);
    AnimancerState* stop(Key key);
    bool isPlaying() const;
    bool isPlaying(const Clip& clip) const;
    bool isPlaying(Key key) const;
    bool isPlayingClip(const Clip& clip) const;
    AnimancerLayer* layer(int index);
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

private:
    Skeleton* _skeleton = nullptr;
    std::unique_ptr<AnimancerPlayable> _playable;
    std::vector<Mat4> _palette;
};

}  // namespace anim
