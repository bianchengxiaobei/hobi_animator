#include <gtest/gtest.h>

#include <array>
#include <stdexcept>
#include <string>

#include "anim/animancer/fsm/DelegateState.h"
#include "anim/animancer/fsm/InputBuffer.h"
#include "anim/animancer/fsm/StateBehaviour.h"
#include "anim/animancer/fsm/StateChange.h"
#include "anim/animancer/fsm/StateMachine.h"
#include "anim/animancer/fsm/StateMachine2.h"
#include "anim/animancer/fsm/WithDefault.h"

using namespace anim::fsm;

namespace {

struct TestState : State {
    std::string name;
    int enters = 0;
    int exits = 0;
    bool allowEnter = true;
    bool allowExit = true;

    TestState* seenPrevious = nullptr;
    TestState* seenNext = nullptr;

    explicit TestState(std::string stateName) : name(std::move(stateName)) {}

    bool canEnterState() const override { return allowEnter; }
    bool canExitState() const override { return allowExit; }

    void onEnterState() override {
        ++enters;
        if (StateChange<TestState>::isActive()) {
            seenPrevious = StateChange<TestState>::previousState();
            seenNext = StateChange<TestState>::nextState();
        }
    }

    void onExitState() override { ++exits; }
};

struct OwnedState : State, IOwnedState<OwnedState> {
    StateMachine<OwnedState>* machine = nullptr;
    int enters = 0;
    OwnedState* seenPrevious = nullptr;

    StateMachine<OwnedState>* ownerStateMachine() override { return machine; }

    void onEnterState() override {
        ++enters;
        if (StateChange<OwnedState>::isActive()) {
            seenPrevious = StateChange<OwnedState>::previousState();
        }
    }
};

struct IntKeyedState : State {
    int enters = 0;
    int keyPrevious = -1;
    int keyNext = -1;

    void onEnterState() override {
        ++enters;
        if (KeyChange<int>::isActive()) {
            keyPrevious = KeyChange<int>::previousKey();
            keyNext = KeyChange<int>::nextKey();
        }
    }
};

struct BehaviourState : StateBehaviour {
    int enables = 0;
    int disables = 0;

protected:
    void onEnable() override { ++enables; }
    void onDisable() override { ++disables; }
};

}  // namespace

TEST(FSM, BasicTransitionsAndGuards) {
    StateMachine<TestState> machine;
    TestState a("a");
    TestState b("b");

    EXPECT_EQ(machine.currentState(), nullptr);
    EXPECT_TRUE(machine.trySetState(&a));
    EXPECT_EQ(machine.currentState(), &a);
    EXPECT_EQ(a.enters, 1);
    EXPECT_EQ(a.seenPrevious, nullptr);
    EXPECT_EQ(a.seenNext, &a);

    EXPECT_TRUE(machine.trySetState(&b));
    EXPECT_EQ(a.exits, 1);
    EXPECT_EQ(b.enters, 1);
    EXPECT_EQ(b.seenPrevious, &a);
    EXPECT_EQ(b.seenNext, &b);

    EXPECT_TRUE(machine.trySetState(&b));
    EXPECT_EQ(b.enters, 1);

    EXPECT_TRUE(machine.tryResetState(&b));
    EXPECT_EQ(b.exits, 1);
    EXPECT_EQ(b.enters, 2);

    a.allowEnter = false;
    EXPECT_FALSE(machine.trySetState(&a));
    EXPECT_EQ(machine.currentState(), &b);

    a.allowEnter = true;
    EXPECT_TRUE(machine.trySetState(&a));
    b.allowEnter = false;
    EXPECT_FALSE(machine.trySetState(&b));
    EXPECT_EQ(machine.currentState(), &a);

    machine.forceSetState(&b);
    EXPECT_EQ(machine.currentState(), &b);
}

TEST(FSM, BatchSelectionUsesFirstAllowed) {
    StateMachine<TestState> machine;
    TestState first("first");
    TestState second("second");
    TestState third("third");
    first.allowEnter = false;
    second.allowEnter = false;

    std::array<TestState*, 3> states{&first, &second, &third};
    EXPECT_EQ(machine.canSetState(states), &third);
    EXPECT_TRUE(machine.trySetState(states));
    EXPECT_EQ(machine.currentState(), &third);
    EXPECT_EQ(third.enters, 1);

    third.allowExit = false;
    EXPECT_FALSE(machine.tryResetState(states));
    EXPECT_EQ(machine.currentState(), &third);
    EXPECT_EQ(third.enters, 1);
}

TEST(FSM, NullStatesRequireOptIn) {
    StateMachine<TestState> machine;
    TestState a("a");
    machine.trySetState(&a);

    EXPECT_THROW(machine.forceSetState(nullptr), std::logic_error);
    machine.setAllowNullStates();
    EXPECT_TRUE(machine.allowNullStates());
    machine.forceSetState(nullptr);
    EXPECT_EQ(machine.currentState(), nullptr);
    EXPECT_EQ(a.exits, 1);
}

TEST(FSM, StateChangeSupportsRecursiveChanges) {
    StateMachine<TestState> machine;
    TestState a("a");
    TestState b("b");
    TestState c("c");

    EXPECT_FALSE(StateChange<TestState>::isActive());
    {
        StateChange<TestState> outer(&machine, &a, &b);
        EXPECT_TRUE(StateChange<TestState>::isActive());
        EXPECT_EQ(StateChange<TestState>::stateMachine(), &machine);
        EXPECT_EQ(StateChange<TestState>::previousState(), &a);
        EXPECT_EQ(StateChange<TestState>::nextState(), &b);

        {
            StateChange<TestState> inner(&machine, &b, &c);
            EXPECT_EQ(StateChange<TestState>::previousState(), &b);
            EXPECT_EQ(StateChange<TestState>::nextState(), &c);
        }

        EXPECT_EQ(StateChange<TestState>::previousState(), &a);
        EXPECT_EQ(StateChange<TestState>::nextState(), &b);
    }
    EXPECT_FALSE(StateChange<TestState>::isActive());
}

TEST(FSM, InitializeAfterDeserializeEntersCurrentState) {
    TestState a("a");
    StateMachine<TestState> machine(&a);
    EXPECT_EQ(a.enters, 1);

    machine.initializeAfterDeserialize();
    EXPECT_EQ(a.enters, 2);
    EXPECT_EQ(machine.currentState(), &a);
}

TEST(FSM, WithDefaultEntersAndResetsDefault) {
    StateMachineWithDefault<TestState> machine;
    TestState idle("idle");
    TestState walk("walk");

    machine.setDefaultState(&idle);
    EXPECT_EQ(machine.currentState(), &idle);
    EXPECT_EQ(machine.defaultState(), &idle);

    machine.trySetState(&walk);
    EXPECT_EQ(machine.currentState(), &walk);
    EXPECT_TRUE(machine.trySetDefaultState());
    EXPECT_EQ(machine.currentState(), &idle);

    EXPECT_TRUE(machine.tryResetDefaultState());
    EXPECT_EQ(idle.enters, 3);

    TestState spawn("spawn");
    machine.setDefaultState(&spawn);
    EXPECT_EQ(machine.currentState(), &idle);
    EXPECT_EQ(machine.defaultState(), &spawn);
}

TEST(FSM, WithDefaultInitializesFromDefaultAfterDeserialize) {
    TestState idle("idle");
    StateMachineWithDefault<TestState> machine;
    machine.setDefaultState(&idle);
    const int enters = idle.enters;

    machine.initializeAfterDeserialize();
    EXPECT_EQ(idle.enters, enters + 1);
    EXPECT_EQ(machine.currentState(), &idle);
}

TEST(FSM, InputBufferRetriesAndTimesOut) {
    StateMachine<TestState> machine;
    TestState a("a");
    TestState b("b");
    InputBuffer<TestState> buffer(&machine);

    buffer.buffer(&a, 0.5f);
    EXPECT_TRUE(buffer.isActive());
    EXPECT_TRUE(buffer.update(0.2f));
    EXPECT_FALSE(buffer.isActive());
    EXPECT_EQ(machine.currentState(), &a);

    a.allowExit = false;
    buffer.buffer(&b, 0.3f);
    EXPECT_FALSE(buffer.update(0.2f));
    EXPECT_TRUE(buffer.isActive());
    EXPECT_FALSE(buffer.update(0.15f));
    EXPECT_FALSE(buffer.isActive());
    EXPECT_EQ(machine.currentState(), &a);

    b.allowEnter = false;
    buffer.buffer(&b, 0.1f);
    EXPECT_FALSE(buffer.update(0.1f));
    EXPECT_TRUE(buffer.isActive());
    EXPECT_FALSE(buffer.update(0.01f));
    EXPECT_FALSE(buffer.isActive());
}

TEST(FSM, KeyedMachineTracksKeysAndChanges) {
    StateMachine<int, IntKeyedState> machine;
    IntKeyedState idle;
    IntKeyedState walk;
    IntKeyedState run;

    machine.add(1, &idle);
    machine.add(2, &walk);
    std::array<int, 1> keys{3};
    std::array<IntKeyedState*, 1> states{&run};
    machine.addRange(keys, states);

    EXPECT_EQ(machine.getState(2), &walk);
    EXPECT_TRUE(machine.containsKey(3));
    EXPECT_EQ(machine.count(), 3u);

    machine.forceSetState(1);
    EXPECT_EQ(machine.currentState(), &idle);
    EXPECT_EQ(machine.currentKey(), 1);

    IntKeyedState* entered = machine.trySetState(2);
    EXPECT_EQ(entered, &walk);
    EXPECT_EQ(machine.currentKey(), 2);
    EXPECT_EQ(walk.keyPrevious, 1);
    EXPECT_EQ(walk.keyNext, 2);

    EXPECT_EQ(machine.tryResetState(99), nullptr);
    EXPECT_EQ(machine.currentKey(), 2);

    machine.setFakeKey(7);
    EXPECT_EQ(machine.currentKey(), 7);
    EXPECT_EQ(machine.currentState(), &walk);

    EXPECT_EQ(machine.forceSetState(3), &run);
    EXPECT_EQ(machine.currentState(), &run);
    EXPECT_EQ(machine.currentKey(), 3);

    machine.initializeAfterDeserialize();
    EXPECT_EQ(run.enters, 2);

    EXPECT_TRUE(machine.remove(3));
    EXPECT_FALSE(machine.containsKey(3));
    machine.clearStates();
    EXPECT_EQ(machine.count(), 0u);
}

TEST(FSM, OwnedStateExtensionsWork) {
    StateMachine<OwnedState> machine;
    OwnedState one;
    OwnedState two;
    one.machine = &machine;
    two.machine = &machine;

    EXPECT_TRUE(tryEnterState(&one));
    EXPECT_TRUE(isCurrentState(&one));
    EXPECT_FALSE(isCurrentState(&two));

    EXPECT_TRUE(tryEnterState(&two));
    EXPECT_EQ(two.seenPrevious, &one);
    EXPECT_TRUE(isCurrentState(&two));

    EXPECT_TRUE(tryReEnterState(&two));
    EXPECT_EQ(two.enters, 2);

    forceEnterState(&one);
    EXPECT_TRUE(isCurrentState(&one));
}

TEST(FSM, DelegateStateAndBehaviour) {
    DelegateState delegate;
    EXPECT_TRUE(delegate.canEnterState());
    EXPECT_TRUE(delegate.canExitState());

    int enters = 0;
    int exits = 0;
    delegate.canEnter = [] { return false; };
    delegate.canExit = [] { return false; };
    delegate.onEnter = [&] { ++enters; };
    delegate.onExit = [&] { ++exits; };

    EXPECT_FALSE(delegate.canEnterState());
    EXPECT_FALSE(delegate.canExitState());
    delegate.onEnterState();
    delegate.onExitState();
    EXPECT_EQ(enters, 1);
    EXPECT_EQ(exits, 1);

    BehaviourState behaviour;
    EXPECT_FALSE(behaviour.isActive());
    behaviour.onEnterState();
    EXPECT_TRUE(behaviour.isActive());
    behaviour.onEnterState();
    EXPECT_EQ(behaviour.enables, 1);

    behaviour.onExitState();
    EXPECT_FALSE(behaviour.isActive());
    behaviour.onExitState();
    EXPECT_EQ(behaviour.disables, 1);
}
