#pragma once

#include "anim/animancer/fsm/IState.h"

namespace anim::fsm {

template <class TKey>
struct IKeyedStateMachine {
    virtual ~IKeyedStateMachine() = default;
    virtual TKey currentKey() const = 0;
};

template <class TKey>
class KeyChange {
public:
    static bool isActive() noexcept { return s_current != nullptr; }

    static IKeyedStateMachine<TKey>* stateMachine() noexcept {
        return s_current != nullptr ? s_current->_machine : nullptr;
    }

    static TKey previousKey() {
        return s_current != nullptr ? s_current->_previous : TKey{};
    }

    static TKey nextKey() {
        return s_current != nullptr ? s_current->_next : TKey{};
    }

    KeyChange(IKeyedStateMachine<TKey>* machine, TKey previous, TKey next) noexcept
        : _outer(s_current), _machine(machine), _previous(std::move(previous)),
          _next(std::move(next)) {
        s_current = this;
    }

    ~KeyChange() { s_current = _outer; }

    KeyChange(const KeyChange&) = delete;
    KeyChange& operator=(const KeyChange&) = delete;

private:
    KeyChange* _outer = nullptr;
    IKeyedStateMachine<TKey>* _machine = nullptr;
    TKey _previous{};
    TKey _next{};

    static thread_local KeyChange* s_current;
};

template <class TKey>
thread_local KeyChange<TKey>* KeyChange<TKey>::s_current = nullptr;

}  // namespace anim::fsm
