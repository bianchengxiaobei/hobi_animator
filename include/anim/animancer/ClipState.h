#pragma once

#include "anim/animancer/AnimancerState.h"
#include "anim/clip/Clip.h"
#include "anim/core/Math.h"

// 单个 Clip 的状态（对应 ClipState）。
//
// 文档：docs/07-Animancer门面.md §3
namespace anim {

class AnimancerPlayable;

class ClipState final : public AnimancerState {
public:
    ClipState(AnimancerPlayable* root, const Clip& clip);

    const Clip* clip() const noexcept override { return _clip; }
    const Clip& clipRef() const noexcept { return *_clip; }
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;
    void setClip(const Clip& clip) noexcept {
        _clip = &clip;
        setKey(Key::fromPointer(_clip));
    }

    float length() const override;
    bool isLooping() const noexcept override;

    void createPlayable() override;

    AnimancerState* clone(AnimancerPlayable* root) override;

private:
    const Clip* _clip = nullptr;
};

}  // namespace anim
