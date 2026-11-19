#pragma once

#include "anim/animancer/fsm/IState.h"

namespace anim::fsm {

class StateBehaviour : public State {
public:
    bool isActive() const noexcept { return _active; }

    void setActive(bool active) {
        if (_active == active) return;
        _active = active;
        if (active)
            onEnable();
        else
            onDisable();
    }

    void onEnterState() override { setActive(true); }
    void onExitState() override { setActive(false); }

protected:
    virtual void onEnable() {}
    virtual void onDisable() {}

private:
    bool _active = false;
};

}  // namespace anim::fsm
