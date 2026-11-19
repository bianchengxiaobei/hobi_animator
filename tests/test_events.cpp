#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "anim/animancer/Event.h"

using namespace anim;

TEST(EventSequence, AddsInSortedOrder) {
    EventSequence sequence;
    EXPECT_TRUE(sequence.isEmpty());
    EXPECT_EQ(sequence.count(), 0);

    const int a = sequence.add(0.5f, [] {});
    const int b = sequence.add(0.2f, [] {});
    const int c = sequence.add(0.8f, [] {});

    EXPECT_EQ(sequence.count(), 3);
    EXPECT_EQ(sequence.at(0).normalizedTime, 0.2f);
    EXPECT_EQ(sequence.at(1).normalizedTime, 0.5f);
    EXPECT_EQ(sequence.at(2).normalizedTime, 0.8f);

    // 返回值是“插入当时”的下标；先插入的事件在后续插入后下标会变化。
    EXPECT_EQ(b, 0);
    EXPECT_EQ(c, 2);
    (void)a;
    EXPECT_FALSE(sequence.isEmpty());
}

TEST(EventSequence, VersionChangesOnlyForGeneralEvents) {
    EventSequence sequence;
    const int version = sequence.version();

    sequence.add(0.5f, [] {});
    EXPECT_EQ(sequence.version(), version + 1);

    sequence.setNormalizedEndTime(0.75f);
    EXPECT_EQ(sequence.version(), version + 1);  // End event 不增加 Version

    sequence.clear();
    EXPECT_EQ(sequence.version(), version + 2);
    EXPECT_TRUE(sequence.isEmpty());
    EXPECT_TRUE(std::isnan(sequence.normalizedEndTime()));
}

TEST(EventSequence, EndEventAndNames) {
    EventSequence sequence;
    sequence.add(0.25f, [] {}, "Jump");
    sequence.add(0.5f, [] {}, "Land");

    EXPECT_EQ(sequence.indexOf("Jump"), 0);
    EXPECT_EQ(sequence.indexOf("Land"), 1);
    EXPECT_EQ(sequence.indexOf("Missing"), -1);

    sequence.endEvent() = AnimancerEvent(1.0f, [] {}, "End");
    EXPECT_EQ(sequence.endEvent().name, "End");
    EXPECT_FLOAT_EQ(sequence.normalizedEndTime(), 1.0f);
}

TEST(EventSequence, CopyFromCopiesEventsAndEnd) {
    EventSequence source;
    source.add(0.25f, [] {}, "A");
    source.endEvent() = AnimancerEvent(0.9f, [] {}, "End");

    EventSequence copy;
    copy.copyFrom(source);

    EXPECT_EQ(copy.count(), 1);
    EXPECT_EQ(copy.at(0).name, "A");
    EXPECT_EQ(copy.endEvent().name, "End");
    EXPECT_FLOAT_EQ(copy.normalizedEndTime(), 0.9f);
}

TEST(EventSequence, DefaultTimesFollowSpeedDirection) {
    EXPECT_FLOAT_EQ(EventSequence::defaultNormalizedStartTime(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(EventSequence::defaultNormalizedStartTime(-1.0f), 1.0f);
    EXPECT_FLOAT_EQ(EventSequence::defaultNormalizedEndTime(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(EventSequence::defaultNormalizedEndTime(-1.0f), 0.0f);
}

TEST(EventSequence, RejectsOutOfRangeIndex) {
    EventSequence sequence;
    EXPECT_THROW(sequence.at(0), std::logic_error);
}
