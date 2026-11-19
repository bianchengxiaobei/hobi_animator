#pragma once

#include "anim/animancer/fsm/IState.h"
#include "anim/core/Assert.h"

namespace anim::fsm {

template <class TState>
class StateChange {
public:
    static bool isActive() noexcept { return s_current != nullptr; }

    static StateMachine<TState>* stateMachine() noexcept {
        return s_current != nullptr ? s_current->_machine : nullptr;
    }

    static TState* previousState() noexcept {
        return s_current != nullptr ? s_current->_previous : nullptr;
    }

    static TState* nextState() noexcept {
        return s_current != nullptr ? s_current->_next : nullptr;
    }

    StateChange(StateMachine<TState>* machine, TState* previous, TState* next) noexcept
        : _outer(s_current), _machine(machine), _previous(previous), _next(next) {
        s_current = this;
    }

    ~StateChange() { s_current = _outer; }

    StateChange(const StateChange&) = delete;
    StateChange& operator=(const StateChange&) = delete;

private:
    StateChange* _outer = nullptr;
    StateMachine<TState>* _machine = nullptr;
    TState* _previous = nullptr;
    TState* _next = nullptr;

    static thread_local StateChange* s_current;
};

template <class TState>
thread_local StateChange<TState>* StateChange<TState>::s_current = nullptr;

}  // namespace anim::fsm
