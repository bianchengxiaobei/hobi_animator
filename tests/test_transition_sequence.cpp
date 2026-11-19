#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "anim/hobi/HobiAnimator.h"
#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/ClipTransitionSequence.h"
#include "anim/hobi/LayerList.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    return skeleton;
}

BoneLocalClip makeClip(float length, bool looping = false, float averageSpeed = 0.0f) {
    BoneLocalClip clip;
    clip.length = length;
    clip.looping = looping;
    clip.averageSpeed = averageSpeed;
    return clip;
}

ClipTransition makeTransition(const Clip& clip, float fadeDuration = 0.0f) {
    ClipTransition transition;
    transition.clip = &clip;
    transition.fadeDurationValue = fadeDuration;
    return transition;
}

}  // namespace

TEST(ClipTransitionSequence, ReportsCombinedProperties) {
    const BoneLocalClip first = makeClip(0.5f, false, 1.0f);
    const BoneLocalClip second = makeClip(1.0f, false, 3.0f);
    const BoneLocalClip third = makeClip(2.0f, true, 5.0f);

    ClipTransitionSequence sequence;
    sequence.clip = &first;
    sequence.others.push_back(makeTransition(second));
    sequence.others.push_back(makeTransition(third));

    EXPECT_TRUE(sequence.isValid());
    EXPECT_FLOAT_EQ(sequence.length(), 3.5f);
    EXPECT_FLOAT_EQ(sequence.maximumDuration(), 3.5f);
    EXPECT_TRUE(sequence.isLooping());
    EXPECT_FLOAT_EQ(sequence.averageAngularSpeed(), 0.0f);

    const Vec3 expectedVelocity(0.0f, 0.0f, (1.0f * 0.5f + 3.0f * 1.0f + 5.0f * 2.0f) / 3.5f);
    EXPECT_NEAR(sequence.averageVelocity().x, expectedVelocity.x, 1e-5f);
    EXPECT_NEAR(sequence.averageVelocity().y, expectedVelocity.y, 1e-5f);
    EXPECT_NEAR(sequence.averageVelocity().z, expectedVelocity.z, 1e-5f);

    EXPECT_EQ(sequence.lastTransition().clip, &third);
    EXPECT_EQ(&sequence.endEvent(), &sequence.others.back().eventSequence.endEvent());
}

TEST(ClipTransitionSequence, InvalidOtherInvalidatesSequence) {
    const BoneLocalClip first = makeClip(0.5f);

    ClipTransitionSequence sequence;
    sequence.clip = &first;
    sequence.others.emplace_back();

    EXPECT_FALSE(sequence.isValid());
}

TEST(ClipTransitionSequence, MovesEndEventToLastTransition) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip first = makeClip(0.5f);
    const BoneLocalClip second = makeClip(0.5f);

    ClipTransitionSequence sequence;
    sequence.clip = &first;
    sequence.others.push_back(makeTransition(second));

    int endCount = 0;
    sequence.eventSequence.endEvent() =
        HobiEvent(std::numeric_limits<float>::quiet_NaN(), [&endCount] { ++endCount; });

    HobiState* state = animator.play(sequence);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(sequence.eventSequence.endEvent().hasCallback());
    EXPECT_TRUE(sequence.others[0].eventSequence.endEvent().hasCallback());

    animator.update(0.6f);
    ASSERT_NE(animator.playable().layers().getLayer(0)->currentState(), nullptr);
    EXPECT_EQ(animator.playable().layers().getLayer(0)->currentState()->clip(), &second);

    animator.update(0.6f);
    EXPECT_EQ(endCount, 1);
}

TEST(ClipTransitionSequence, PlaysTransitionsInOrder) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip first = makeClip(0.5f);
    const BoneLocalClip second = makeClip(0.5f);
    const BoneLocalClip third = makeClip(0.5f);

    ClipTransitionSequence sequence;
    sequence.clip = &first;
    sequence.others.push_back(makeTransition(second));
    sequence.others.push_back(makeTransition(third));

    ASSERT_NE(animator.play(sequence), nullptr);
    HobiLayer* layer = animator.playable().layers().getLayer(0);

    animator.update(0.6f);
    ASSERT_NE(layer->currentState(), nullptr);
    EXPECT_EQ(layer->currentState()->clip(), &second);

    animator.update(0.6f);
    ASSERT_NE(layer->currentState(), nullptr);
    EXPECT_EQ(layer->currentState()->clip(), &third);
}

TEST(ClipTransitionSequence, AddsEventsRelativeToWholeSequence) {
    const BoneLocalClip first = makeClip(1.0f);
    const BoneLocalClip second = makeClip(2.0f);
    const BoneLocalClip third = makeClip(3.0f);

    ClipTransitionSequence sequence;
    sequence.clip = &first;
    sequence.others.push_back(makeTransition(second));
    sequence.others.push_back(makeTransition(third));

    int callbackCount = 0;
    sequence.addEvent(1.5f, false, [&callbackCount] { ++callbackCount; });

    EXPECT_EQ(sequence.others[0].eventSequence.count(), 1);
    EXPECT_NEAR(sequence.others[0].eventSequence.at(0).normalizedTime, 0.25f, 1e-5f);
    EXPECT_EQ(sequence.others[1].eventSequence.count(), 0);
    EXPECT_EQ(callbackCount, 0);

    sequence.addEvent(0.75f, true, [&callbackCount] { ++callbackCount; });

    EXPECT_EQ(sequence.others[1].eventSequence.count(), 1);
    EXPECT_NEAR(sequence.others[1].eventSequence.at(0).normalizedTime, 0.5f, 1e-5f);
}

TEST(ClipTransitionSequence, CopyFromCopiesOthers) {
    const BoneLocalClip first = makeClip(1.0f);
    const BoneLocalClip second = makeClip(2.0f);

    ClipTransitionSequence source;
    source.clip = &first;
    source.others.push_back(makeTransition(second));

    ClipTransitionSequence copy;
    copy.copyFrom(source);

    ASSERT_EQ(copy.others.size(), 1u);
    EXPECT_EQ(copy.clip, &first);
    EXPECT_EQ(copy.others[0].clip, &second);
    EXPECT_FLOAT_EQ(copy.length(), 3.0f);
}
