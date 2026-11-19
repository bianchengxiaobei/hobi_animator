#pragma once

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"

// 相位同步（对应 Utilities/TimeSynchronizer.cs）。
//
// 用法：
//   synchronizer.storeTime(layer);
//   HobiState* state = layer->play(clip);
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

    void storeTime(const HobiLayer* layer) {
        storeTime(layer != nullptr ? layer->currentState() : nullptr);
    }

    void storeTime(const HobiState* state) {
        normalizedTime = state != nullptr ? state->normalizedTimeD() : 0.0;
    }

    bool syncTime(const HobiLayer* layer, const T& group) {
        HobiState* state = layer != nullptr ? layer->currentState() : nullptr;
        return syncTime(state, group, deltaTimeOf(state));
    }

    bool syncTime(const HobiLayer* layer, const T& group, float deltaTime) {
        return syncTime(layer != nullptr ? layer->currentState() : nullptr, group, deltaTime);
    }

    bool syncTime(HobiState* state, const T& group) {
        return syncTime(state, group, deltaTimeOf(state));
    }

    bool syncTime(HobiState* state, const T& group, float deltaTime) {
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
    static float deltaTimeOf(HobiState* state) {
        return state != nullptr && state->root() != nullptr ? state->root()->deltaTime() : 0.0f;
    }
};

}  // namespace anim
