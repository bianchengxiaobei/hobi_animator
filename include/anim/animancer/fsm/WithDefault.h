#pragma once

#include <functional>

#include "anim/animancer/fsm/StateMachine.h"

namespace anim::fsm {

template <class TState>
class StateMachineWithDefault : public StateMachine<TState> {
public:
    StateMachineWithDefault() = default;

    explicit StateMachineWithDefault(TState* defaultState)
        : _defaultState(defaultState) {
        ANIM_CHECK(defaultState != nullptr);
        this->forceSetState(defaultState);
    }

    TState* defaultState() const noexcept { return _defaultState; }

    void setDefaultState(TState* state) {
        _defaultState = state;
        if (this->currentState() == nullptr && state != nullptr) {
            this->forceSetState(state);
        }
    }

    void initializeAfterDeserialize() override {
        if (this->currentState() != nullptr) {
            StateChange<TState> change(this, nullptr, this->currentState());
            this->currentState()->onEnterState();
            return;
        }

        if (_defaultState != nullptr) {
            StateChange<TState> change(this, nullptr, _defaultState);
            this->_currentState = _defaultState;
            _defaultState->onEnterState();
        }
    }

    bool trySetDefaultState() { return this->trySetState(_defaultState); }
    bool tryResetDefaultState() { return this->tryResetState(_defaultState); }

    void forceSetDefaultState() { this->forceSetState(_defaultState); }

private:
    TState* _defaultState = nullptr;
};

}  // namespace anim::fsm
