#pragma once

#include <functional>

#include "anim/core/IUpdatable.h"

// 节点退出事件（对应 Utilities/ExitEvent.cs）。
//
// 在节点 EffectiveWeight 变为 0 时的 post-update 触发回调。事件不会因为
// 播放其它动画而自动清除，需要显式 unregister 或等回调触发。
namespace anim {

class AnimancerNode;
class AnimancerPlayable;

class ExitEvent final : public IUpdatable {
public:
    using Callback = std::function<void()>;

    ExitEvent() = default;

    static ExitEvent* registerEvent(AnimancerNode& node, Callback callback);
    static bool unregister(AnimancerPlayable& playable);
    static bool unregister(AnimancerNode& node);
    static int unregisterAll(AnimancerNode& node);

    bool isActive() const noexcept;

    void update(bool& needsMoreUpdates) override;

private:
    void release();

    AnimancerNode* _node = nullptr;
    AnimancerPlayable* _root = nullptr;
    Callback _callback;
};

}  // namespace anim
