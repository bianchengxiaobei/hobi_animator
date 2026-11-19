#pragma once

// 可更新对象（对应参考实现的 IUpdatable）。
// pre/post 列表在 HobiPlayable 中驱动；needsMoreUpdates 用于同一帧内继续淡入淡出。
namespace anim {

struct IUpdatable {
    virtual ~IUpdatable() = default;
    virtual void update(bool& needsMoreUpdates) = 0;

    // true 表示即使本帧不再需要额外 pass，也应保留在 pre-update 列表里
    // （例如正在播放的 HobiState 需要每帧推进时间）。
    virtual bool requiresContinuousUpdate() const noexcept { return false; }
};

}  // namespace anim
