#pragma once

#include <functional>

#include "anim/core/IUpdatable.h"

// 节点退出事件（对应 Utilities/ExitEvent.cs）。
//
// 在节点 EffectiveWeight 变为 0 时的 post-update 触发回调。事件不会因为
// 播放其它动画而自动清除，需要显式 unregister 或等回调触发。
namespace anim {

class HobiNode;
class HobiPlayable;

class ExitEvent final : public IUpdatable {
public:
    using Callback = std::function<void()>;

    ExitEvent() = default;

    static ExitEvent* registerEvent(HobiNode& node, Callback callback);
    static bool unregister(HobiPlayable& playable);
    static bool unregister(HobiNode& node);
    static int unregisterAll(HobiNode& node);

    bool isActive() const noexcept;

    void update(bool& needsMoreUpdates) override;

private:
    void release();

    HobiNode* _node = nullptr;
    HobiPlayable* _root = nullptr;
    Callback _callback;
};

}  // namespace anim
