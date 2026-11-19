#pragma once

#include <functional>
#include <map>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "anim/animancer/fsm/StateChange.h"

namespace anim::fsm {

template <class... Ts>
class StateMachine;

template <class TState>
class StateMachine<TState> {
    static_assert(std::is_base_of_v<IState, TState>, "TState must derive from IState");

public:
    StateMachine() = default;

    explicit StateMachine(TState* state) {
        ANIM_CHECK(state != nullptr);
        StateChange<TState> change(this, nullptr, state);
        _currentState = state;
        state->onEnterState();
    }

    virtual ~StateMachine() = default;

    // 按优先级从高到低尝试状态（对应 StateMachine<TState>.StateSelector）。
    class StateSelector {
    public:
        using Map = std::map<float, TState*, std::greater<float>>;
        using const_iterator = typename Map::const_iterator;

        StateSelector() = default;

        void add(float priority, TState* state) {
            ANIM_CHECK_MSG(state != nullptr, "state is null");
            const auto result = _states.emplace(priority, state);
            ANIM_CHECK_MSG(result.second, "StateSelector already contains this priority");
        }

        template <class TPrioritizable>
        void add(TPrioritizable* state) {
            static_assert(std::is_base_of_v<TState, TPrioritizable>,
                          "TPrioritizable must derive from TState");
            static_assert(std::is_base_of_v<IPrioritizable, TPrioritizable>,
                          "TPrioritizable must implement IPrioritizable");
            ANIM_CHECK_MSG(state != nullptr, "state is null");
            add(state->priority(), static_cast<TState*>(state));
        }

        std::vector<TState*> values() const {
            std::vector<TState*> result;
            result.reserve(_states.size());
            for (const auto& entry : _states) result.push_back(entry.second);
            return result;
        }

        std::size_t size() const noexcept { return _states.size(); }
        bool empty() const noexcept { return _states.empty(); }
        void clear() noexcept { _states.clear(); }

        const_iterator begin() const noexcept { return _states.begin(); }
        const_iterator end() const noexcept { return _states.end(); }

    private:
        Map _states;
    };

    TState* currentState() const noexcept { return _currentState; }
    TState* previousState() const noexcept { return StateChange<TState>::previousState(); }
    TState* nextState() const noexcept { return StateChange<TState>::nextState(); }

    virtual void initializeAfterDeserialize() {
        if (_currentState == nullptr) return;
        StateChange<TState> change(this, nullptr, _currentState);
        _currentState->onEnterState();
    }

    bool canSetState(TState* state) {
        if (state == nullptr && !_allowNullStates) return false;

        StateChange<TState> change(this, _currentState, state);

        if (_currentState != nullptr && !_currentState->canExitState()) return false;
        if (state != nullptr && !state->canEnterState()) return false;
        return true;
    }

    TState* canSetState(std::span<TState*> states) {
        for (TState* state : states) {
            if (canSetState(state)) return state;
        }
        return nullptr;
    }

    TState* canSetState(const StateSelector& selector) {
        for (const auto& entry : selector) {
            if (canSetState(entry.second)) return entry.second;
        }
        return nullptr;
    }

    bool trySetState(TState* state) {
        if (_currentState == state) {
            if (state == nullptr) return _allowNullStates;
            return true;
        }
        return tryResetState(state);
    }

    bool trySetState(std::span<TState*> states) {
        for (TState* state : states) {
            if (trySetState(state)) return true;
        }
        return false;
    }

    bool trySetState(const StateSelector& selector) {
        for (const auto& entry : selector) {
            if (trySetState(entry.second)) return true;
        }
        return false;
    }

    bool tryResetState(TState* state) {
        if (!canSetState(state)) return false;
        forceSetState(state);
        return true;
    }

    bool tryResetState(std::span<TState*> states) {
        for (TState* state : states) {
            if (tryResetState(state)) return true;
        }
        return false;
    }

    bool tryResetState(const StateSelector& selector) {
        for (const auto& entry : selector) {
            if (tryResetState(entry.second)) return true;
        }
        return false;
    }

    void forceSetState(TState* state) {
        if (state == nullptr) {
            ANIM_CHECK_MSG(_allowNullStates, "state machine does not allow null states");
        } else {
            auto* owned = dynamic_cast<IOwnedState<TState>*>(state);
            ANIM_CHECK_MSG(owned == nullptr || owned->ownerStateMachine() == this,
                           "state belongs to a different state machine");
        }

        StateChange<TState> change(this, _currentState, state);

        if (_currentState != nullptr) _currentState->onExitState();
        _currentState = state;
        if (state != nullptr) state->onEnterState();
    }

    void setAllowNullStates(bool allow = true) noexcept { _allowNullStates = allow; }
    bool allowNullStates() const noexcept { return _allowNullStates; }

    std::string toString() const {
        return std::string("StateMachine -> ") +
               (_currentState != nullptr ? "state" : "null");
    }

protected:
    TState* _currentState = nullptr;

private:
    bool _allowNullStates = false;
};

}  // namespace anim::fsm
