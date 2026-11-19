#pragma once

#include "anim/hobi/HobiState.h"

namespace anim {

class HobiPlayable;
struct Clip;
struct HumanoidAvatar;
class Skeleton;

class RetargetClipState final : public HobiState {
public:
    RetargetClipState(HobiPlayable* root, const Clip& clip,
                      const Skeleton& sourceSkeleton, const HumanoidAvatar& sourceAvatar,
                      const HumanoidAvatar& targetAvatar);

    const Clip* clip() const noexcept override { return _clip; }
    const Clip& clipRef() const noexcept { return *_clip; }
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

    float length() const override;
    bool isLooping() const noexcept override;

    void createPlayable() override;
    HobiState* clone(HobiPlayable* root) override;

private:
    const Clip* _clip = nullptr;
    const Skeleton* _sourceSkeleton = nullptr;
    const HumanoidAvatar* _sourceAvatar = nullptr;
    const HumanoidAvatar* _targetAvatar = nullptr;
};

}  // namespace anim
