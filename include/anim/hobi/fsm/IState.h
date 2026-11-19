#pragma once

#include <utility>

namespace anim::fsm {

template <class... Ts>
class StateMachine;

template <class TState>
class StateChange;

struct IState {
    virtual ~IState() = default;

    virtual bool canEnterState() const = 0;
    virtual bool canExitState() const = 0;

    virtual void onEnterState() = 0;
    virtual void onExitState() = 0;
};

// 可按优先级选择的 state（对应 Animancer.FSM.IPrioritizable）。
struct IPrioritizable : virtual IState {
    virtual float priority() const = 0;
};

class State : public virtual IState {
public:
    bool canEnterState() const override { return true; }
    bool canExitState() const override { return true; }

    void onEnterState() override {}
    void onExitState() override {}
};

template <class TState>
struct IOwnedState {
    virtual ~IOwnedState() = default;
    virtual StateMachine<TState>* ownerStateMachine() = 0;
};

template <class TState>
bool isCurrentState(TState* state) {
    if (state == nullptr) return false;
    auto* owned = dynamic_cast<IOwnedState<TState>*>(state);
    return owned != nullptr && owned->ownerStateMachine() != nullptr &&
           owned->ownerStateMachine()->currentState() == state;
}

template <class TState>
bool tryEnterState(TState* state) {
    return state != nullptr && state->ownerStateMachine()->trySetState(state);
}

template <class TState>
bool tryReEnterState(TState* state) {
    return state != nullptr && state->ownerStateMachine()->tryResetState(state);
}

template <class TState>
void forceEnterState(TState* state) {
    if (state != nullptr) state->ownerStateMachine()->forceSetState(state);
}

template <class TState>
TState* getPreviousState() {
    return StateChange<TState>::previousState();
}

template <class TState>
TState* getNextState() {
    return StateChange<TState>::nextState();
}

}  // namespace anim::fsm
