#pragma once

#include <span>
#include <string>
#include <unordered_map>
#include <utility>

#include "anim/animancer/fsm/KeyChange.h"
#include "anim/animancer/fsm/StateMachine.h"

namespace anim::fsm {

template <class TKey, class TState>
class StateMachine<TKey, TState> : public StateMachine<TState>,
                                   public IKeyedStateMachine<TKey> {
    using Base = StateMachine<TState>;

public:
    StateMachine() = default;

    explicit StateMachine(std::unordered_map<TKey, TState*> states)
        : _states(std::move(states)) {}

    StateMachine(TKey defaultKey, TState* defaultState) {
        _states.emplace(defaultKey, defaultState);
        forceSetState(defaultKey, defaultState);
    }

    StateMachine(std::unordered_map<TKey, TState*> states, TKey defaultKey,
                 TState* defaultState)
        : _states(std::move(states)) {
        _states[defaultKey] = defaultState;
        forceSetState(defaultKey, defaultState);
    }

    TKey currentKey() const override { return _currentKey; }
    TKey previousKey() const { return KeyChange<TKey>::previousKey(); }
    TKey nextKey() const { return KeyChange<TKey>::nextKey(); }

    void initializeAfterDeserialize() override {
        if (this->currentState() != nullptr) {
            KeyChange<TKey> keyChange(this, TKey{}, _currentKey);
            StateChange<TState> stateChange(this, nullptr, this->currentState());
            this->currentState()->onEnterState();
            return;
        }

        const auto it = _states.find(_currentKey);
        if (it != _states.end()) forceSetState(_currentKey, it->second);
    }

    bool trySetState(TKey key, TState* state) {
        if (this->currentState() == state) return true;
        return tryResetState(key, state);
    }

    TState* trySetState(TKey key) {
        if (_currentKey == key) return this->currentState();
        return tryResetState(key);
    }

    bool tryResetState(TKey key, TState* state) {
        KeyChange<TKey> change(this, _currentKey, key);
        if (!this->canSetState(state)) return false;

        _currentKey = key;
        Base::forceSetState(state);
        return true;
    }

    TState* tryResetState(TKey key) {
        const auto it = _states.find(key);
        if (it == _states.end()) return nullptr;
        return tryResetState(key, it->second) ? it->second : nullptr;
    }

    void forceSetState(TKey key, TState* state) {
        KeyChange<TKey> change(this, _currentKey, key);
        _currentKey = key;
        Base::forceSetState(state);
    }

    TState* forceSetState(TKey key) {
        const auto it = _states.find(key);
        TState* state = it != _states.end() ? it->second : nullptr;
        forceSetState(key, state);
        return state;
    }

    TState* getState(TKey key) const {
        const auto it = _states.find(key);
        return it != _states.end() ? it->second : nullptr;
    }

    void add(TKey key, TState* state) { _states[key] = state; }
    bool remove(TKey key) { return _states.erase(key) != 0; }
    bool containsKey(TKey key) const { return _states.find(key) != _states.end(); }
    void clearStates() noexcept { _states.clear(); }
    std::size_t count() const noexcept { return _states.size(); }

    void addRange(std::span<const TKey> keys, std::span<TState*> states) {
        ANIM_CHECK(keys.size() == states.size());
        for (std::size_t i = 0; i < keys.size(); ++i) {
            _states[keys[i]] = states[i];
        }
    }

    void setFakeKey(TKey key) noexcept { _currentKey = key; }

    std::string toString() const {
        return std::string("KeyedStateMachine -> ") +
               (this->currentState() != nullptr ? "state" : "null");
    }

private:
    std::unordered_map<TKey, TState*> _states;
    TKey _currentKey{};
};

}  // namespace anim::fsm
