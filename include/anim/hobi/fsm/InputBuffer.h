#pragma once

#include "anim/hobi/fsm/StateMachine.h"

namespace anim::fsm {

template <class TState>
class InputBuffer {
public:
    InputBuffer() = default;
    explicit InputBuffer(StateMachine<TState>* stateMachine) : _stateMachine(stateMachine) {}

    StateMachine<TState>* stateMachine() const noexcept { return _stateMachine; }

    void setStateMachine(StateMachine<TState>* stateMachine) {
        _stateMachine = stateMachine;
        clear();
    }

    TState* state() const noexcept { return _state; }
    float timeOut() const noexcept { return _timeOut; }
    bool isActive() const noexcept { return _state != nullptr; }

    void buffer(TState* state, float timeOut) {
        _state = state;
        _timeOut = timeOut;
    }

    bool update(float deltaTime) {
        if (_state == nullptr) return false;

        if (tryEnterState()) {
            clear();
            return true;
        }

        _timeOut -= deltaTime;
        if (_timeOut < 0.0f) clear();
        return false;
    }

    void clear() noexcept {
        _state = nullptr;
        _timeOut = 0.0f;
    }

protected:
    virtual bool tryEnterState() {
        return _stateMachine != nullptr && _stateMachine->tryResetState(_state);
    }

private:
    StateMachine<TState>* _stateMachine = nullptr;
    TState* _state = nullptr;
    float _timeOut = 0.0f;
};

}  // namespace anim::fsm
