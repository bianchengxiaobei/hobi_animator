#pragma once

#include "anim/animancer/AnimancerLayer.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"

// 相位同步（对应 Utilities/TimeSynchronizer.cs）。
//
// 用法：
//   synchronizer.storeTime(layer);
//   AnimancerState* state = layer->play(clip);
//   synchronizer.syncTime(state, group);
//
// group 变化后的第一次同步不会应用时间，这与原项目一致：调用方通常先
// StoreTime 再 Play 新动画，SyncTime 只负责判断是否属于同一组。
namespace anim {

template <class T>
class TimeSynchronizer {
public:
    T currentGroup{};
    bool synchronizeDefaultGroup = false;
    double normalizedTime = 0.0;

    TimeSynchronizer() = default;
    explicit TimeSynchronizer(const T& group, bool synchronizeDefaultGroup = false)
        : currentGroup(group), synchronizeDefaultGroup(synchronizeDefaultGroup) {}

    void storeTime(const AnimancerLayer* layer) {
        storeTime(layer != nullptr ? layer->currentState() : nullptr);
    }

    void storeTime(const AnimancerState* state) {
        normalizedTime = state != nullptr ? state->normalizedTimeD() : 0.0;
    }

    bool syncTime(const AnimancerLayer* layer, const T& group) {
        AnimancerState* state = layer != nullptr ? layer->currentState() : nullptr;
        return syncTime(state, group, deltaTimeOf(state));
    }

    bool syncTime(const AnimancerLayer* layer, const T& group, float deltaTime) {
        return syncTime(layer != nullptr ? layer->currentState() : nullptr, group, deltaTime);
    }

    bool syncTime(AnimancerState* state, const T& group) {
        return syncTime(state, group, deltaTimeOf(state));
    }

    bool syncTime(AnimancerState* state, const T& group, float deltaTime) {
        if (state == nullptr || currentGroup != group ||
            (!synchronizeDefaultGroup && group == T{})) {
            currentGroup = group;
            return false;
        }

        state->setTimeD(normalizedTime * static_cast<double>(state->length()) +
                        static_cast<double>(deltaTime) * state->effectiveSpeed());
        return true;
    }

private:
    static float deltaTimeOf(AnimancerState* state) {
        return state != nullptr && state->root() != nullptr ? state->root()->deltaTime() : 0.0f;
    }
};

}  // namespace anim
