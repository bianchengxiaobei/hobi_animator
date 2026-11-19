#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "anim/clip/Clip.h"
#include "anim/hobi/Event.h"
#include "anim/hobi/EventObserver.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    return skeleton;
}

BoneLocalClip makeClip(float length, bool looping = false) {
    BoneLocalClip clip;
    clip.length = length;
    clip.looping = looping;
    return clip;
}

class RecordingObserver final : public IEventObserver {
public:
    struct Record {
        const HobiState* state = nullptr;
        EventKind kind = EventKind::Normal;
        int eventIndex = 0;
        bool playingForwards = true;
        float normalizedTime = 0.0f;
        std::string name;
        uint64_t frameId = 0;
    };

    std::vector<Record> records;
    std::vector<std::string>* order = nullptr;

    void onEventDispatched(const HobiState& state, const HobiEvent& event,
                           EventKind kind, int eventIndex,
                           bool playingForwards) override {
        if (order != nullptr) order->push_back("observer");
        records.push_back(Record{&state, kind, eventIndex, playingForwards,
                                 event.normalizedTime, event.name,
                                 state.root() != nullptr ? state.root()->frameId() : 0});
    }
};

}

TEST(EventObserver, NotifiesBeforeCallbackForNormalAndEndEvents) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    std::vector<std::string> order;
    EventSequence events;
    events.add(0.5f, [&order] { order.push_back("callback"); }, "Mid");
    events.endEvent() = HobiEvent(1.0f, [&order] { order.push_back("end"); }, "End");
    state->setEvents(&events);

    RecordingObserver observer;
    observer.order = &order;
    root.setEventObserver(&observer);
    EXPECT_EQ(root.eventObserver(), &observer);

    root.evaluate(0.6f);
    ASSERT_EQ(observer.records.size(), 1u);
    const RecordingObserver::Record& mid = observer.records[0];
    EXPECT_EQ(mid.state, state);
    EXPECT_EQ(mid.kind, EventKind::Normal);
    EXPECT_EQ(mid.eventIndex, 0);
    EXPECT_TRUE(mid.playingForwards);
    EXPECT_FLOAT_EQ(mid.normalizedTime, 0.5f);
    EXPECT_EQ(mid.name, "Mid");
    EXPECT_EQ(mid.frameId, root.frameId());
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "observer");
    EXPECT_EQ(order[1], "callback");

    root.evaluate(0.5f);
    ASSERT_EQ(observer.records.size(), 2u);
    const RecordingObserver::Record& end = observer.records[1];
    EXPECT_EQ(end.kind, EventKind::End);
    EXPECT_EQ(end.eventIndex, -1);
    EXPECT_TRUE(end.playingForwards);
    EXPECT_EQ(end.name, "End");
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[2], "observer");
    EXPECT_EQ(order[3], "end");
}

TEST(EventObserver, RecordsBackwardsAndLoopingEvents) {
    Skeleton skeleton = makeSkeleton();

    HobiPlayable root;
    root.createOutput(skeleton);
    BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    EventSequence events;
    events.add(0.5f, [] {});
    state->setEvents(&events);
    state->setTimeD(1.0);
    state->setSpeed(-1.0f);

    RecordingObserver observer;
    root.setEventObserver(&observer);
    root.evaluate(0.6f);

    ASSERT_EQ(observer.records.size(), 1u);
    EXPECT_EQ(observer.records[0].kind, EventKind::Normal);
    EXPECT_EQ(observer.records[0].eventIndex, 0);
    EXPECT_FALSE(observer.records[0].playingForwards);

    HobiPlayable loopRoot;
    loopRoot.createOutput(skeleton);
    BoneLocalClip loopClip = makeClip(1.0f, true);
    HobiState* loopState = loopRoot.play(loopClip);
    ASSERT_NE(loopState, nullptr);

    EventSequence loopEvents;
    loopEvents.add(0.25f, [] {});
    loopState->setEvents(&loopEvents);

    RecordingObserver loopObserver;
    loopRoot.setEventObserver(&loopObserver);
    loopRoot.evaluate(0.3f);
    loopRoot.evaluate(1.0f);

    ASSERT_GE(loopObserver.records.size(), 2u);
    for (const RecordingObserver::Record& record : loopObserver.records) {
        EXPECT_EQ(record.kind, EventKind::Normal);
        EXPECT_EQ(record.eventIndex, 0);
        EXPECT_TRUE(record.playingForwards);
    }
}

TEST(EventObserver, CanDetachAndObserveNewStates) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    RecordingObserver observer;
    root.setEventObserver(&observer);

    BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    EventSequence events;
    events.add(0.25f, [] {});
    state->setEvents(&events);
    root.evaluate(0.3f);
    ASSERT_EQ(observer.records.size(), 1u);

    root.setEventObserver(nullptr);
    state->setTimeD(0.0);
    root.evaluate(0.3f);
    EXPECT_EQ(observer.records.size(), 1u);
    EXPECT_EQ(root.eventObserver(), nullptr);
}

TEST(EventObserver, DoesNotNotifyUncalledEndEvent) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clip = makeClip(0.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    EventSequence events;
    state->setEvents(&events);

    RecordingObserver observer;
    root.setEventObserver(&observer);
    root.evaluate(0.1f);

    EXPECT_TRUE(observer.records.empty());
}
