#pragma once

#include "anim/core/IUpdatable.h"

// 调试用：检测并取消不应该发生的淡入淡出（对应 Utilities/Custom Fade/DontAllowFade.cs）。
namespace anim {

class AnimancerNode;
class AnimancerPlayable;

class DontAllowFade final : public IUpdatable {
public:
    explicit DontAllowFade(AnimancerPlayable& playable);

    // 始终启用；适用于 sprite 角色等不需要淡入淡出的场景。
    static void enable(AnimancerPlayable& playable);

    // 仅 debug 构建启用（对应原项目 [Conditional]）。
    static void assertEnabled(AnimancerPlayable& playable);

    void update(bool& needsMoreUpdates) override;
    bool requiresContinuousUpdate() const noexcept override { return true; }

private:
    static void validate(AnimancerNode& node);

    AnimancerPlayable* _playable = nullptr;
};

}  // namespace anim
