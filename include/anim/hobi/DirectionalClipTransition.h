#pragma once

#include "anim/hobi/DirectionalAnimationSet.h"
#include "anim/hobi/DirectionalAnimationSet8.h"
#include "anim/hobi/Transition.h"

// 从 DirectionalAnimationSet 选择 Clip 的过渡（对应 DirectionalClipTransition.cs）。
namespace anim {

class DirectionalClipTransition : public ClipTransition {
public:
    const DirectionalAnimationSet* animationSet = nullptr;

    void setDirection(Vec2 direction);
    void setDirection(int direction);
    void setDirection(DirectionalAnimationSet::Direction direction);
    void setDirection(DirectionalAnimationSet8::Direction direction);

    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

    using ClipTransition::copyFrom;
    void copyFrom(const DirectionalClipTransition& other);
};

}  // namespace anim
