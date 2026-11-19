#pragma once

#include <memory>
#include <vector>

#include "anim/hobi/AnimationClipCollection.h"
#include "anim/hobi/FadeMode.h"
#include "anim/hobi/Key.h"
#include "anim/core/Math.h"
#include "anim/skeleton/RootMotion.h"

// 组件等价物（对应 HobiComponent 的核心职责）。
//
// 文档：docs/07-Hobi门面.md §10
namespace anim {

class HobiLayer;
class HobiPlayable;
class HobiState;
struct Clip;
struct HumanoidAvatar;
class ITransition;
class Skeleton;

class HobiAnimator : public IAnimationClipCollection {
public:
    HobiAnimator();
    ~HobiAnimator();

    HobiAnimator(const HobiAnimator&) = delete;
    HobiAnimator& operator=(const HobiAnimator&) = delete;

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

    HobiPlayable& playable() { return *_playable; }
    const HobiPlayable& playable() const { return *_playable; }
    Skeleton* skeleton() const noexcept { return _skeleton; }

    const std::vector<Mat4>& palette() const noexcept { return _palette; }

    // 由外部决定是否消费；本库不自动修改任何 Transform（对应原项目
    // RedirectRootMotionToTransform 的可选性）。
    RootMotionDelta consumeRootMotion() const;

    HobiState* play(const Clip& clip);
    HobiState* play(const Clip& clip, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    HobiState* play(ITransition& transition);
    HobiState* play(ITransition& transition, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    HobiState* play(HobiState* state);
    HobiState* play(HobiState* state, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    HobiState* tryPlay(Key key);
    HobiState* tryPlay(Key key, float fadeDuration,
                            FadeMode mode = FadeMode::FixedSpeed);
    void stop();
    HobiState* stop(const Clip& clip);
    HobiState* stop(Key key);
    bool isPlaying() const;
    bool isPlaying(const Clip& clip) const;
    bool isPlaying(Key key) const;
    bool isPlayingClip(const Clip& clip) const;
    HobiLayer* layer(int index);
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

private:
    Skeleton* _skeleton = nullptr;
    std::unique_ptr<HobiPlayable> _playable;
    std::vector<Mat4> _palette;
};

}  // namespace anim
