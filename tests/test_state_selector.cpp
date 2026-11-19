#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "anim/animancer/fsm/StateMachine.h"

using namespace anim::fsm;

namespace {

struct PrioritizedState : State, IPrioritizable {
    float priorityValue = 0.0f;
    bool allowEnter = true;
    int enters = 0;

    explicit PrioritizedState(float priority) : priorityValue(priority) {}

    float priority() const override { return priorityValue; }
    bool canEnterState() const override { return allowEnter; }

    void onEnterState() override { ++enters; }
};

}  // namespace

TEST(StateSelector, OrdersByDescendingPriority) {
    StateMachine<PrioritizedState>::StateSelector selector;
    PrioritizedState low(0.0f);
    PrioritizedState high(10.0f);
    PrioritizedState middle(5.0f);

    selector.add(&low);
    selector.add(&high);
    selector.add(&middle);

    ASSERT_EQ(selector.size(), 3u);
    const std::vector<PrioritizedState*> values = selector.values();
    EXPECT_EQ(values[0], &high);
    EXPECT_EQ(values[1], &middle);
    EXPECT_EQ(values[2], &low);
}

TEST(StateSelector, StateMachineTriesHighestAllowedState) {
    StateMachine<PrioritizedState> machine;
    StateMachine<PrioritizedState>::StateSelector selector;
    PrioritizedState idle(0.0f);
    PrioritizedState run(10.0f);
    run.allowEnter = false;

    selector.add(&idle);
    selector.add(&run);

    EXPECT_EQ(machine.canSetState(selector), &idle);
    EXPECT_TRUE(machine.trySetState(selector));
    EXPECT_EQ(machine.currentState(), &idle);

    machine.setAllowNullStates();
    machine.forceSetState(nullptr);
    run.allowEnter = true;
    EXPECT_TRUE(machine.trySetState(selector));
    EXPECT_EQ(machine.currentState(), &run);
}

TEST(StateSelector, RejectsDuplicatePriority) {
    StateMachine<PrioritizedState>::StateSelector selector;
    PrioritizedState first(1.0f);
    PrioritizedState second(1.0f);

    selector.add(&first);
    EXPECT_THROW(selector.add(&second), std::logic_error);
}
