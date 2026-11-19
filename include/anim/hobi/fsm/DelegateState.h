#pragma once

#include <functional>

#include "anim/hobi/fsm/IState.h"

namespace anim::fsm {

class DelegateState : public IState {
public:
    std::function<bool()> canEnter;
    std::function<bool()> canExit;
    std::function<void()> onEnter;
    std::function<void()> onExit;

    bool canEnterState() const override { return !canEnter || canEnter(); }
    bool canExitState() const override { return !canExit || canExit(); }

    void onEnterState() override {
        if (onEnter) onEnter();
    }

    void onExitState() override {
        if (onExit) onExit();
    }
};

}  // namespace anim::fsm
