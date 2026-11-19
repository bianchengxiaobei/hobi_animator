#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "anim/hobi/HobiAnimator.h"
#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/Transition.h"
#include "anim/avatar/HumanoidAvatar.h"
#include "anim/avatar/HumanoidIK.h"
#include "anim/clip/Clip.h"
#include "anim/graph/PlayableNode.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    return skeleton;
}

BoneLocalClip makeLinearClip(float length, float from = 0.0f, float to = 10.0f, bool looping = false) {
    BoneLocalClip clip;
    clip.length = length;
    clip.looping = looping;

    BoneChannel channel;
    channel.boneIndex = 0;
    channel.hasPosition = 1;
    const float slope = (to - from) / (length > 0.0f ? length : 1.0f);
    channel.position[0].keys = {
        Keyframe{0.0f, from, slope, slope},
        Keyframe{length, to, slope, slope},
    };

    clip.channels.push_back(channel);
    return clip;
}

}  // namespace

TEST(Hobi, PlaysAndAdvancesClipTime) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiState* state = animator.play(clip);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->isPlaying());
    EXPECT_FLOAT_EQ(state->weight(), 1.0f);

    animator.update(0.25f);
    EXPECT_NEAR(state->time(), 0.25f, 1e-5f);
    EXPECT_NEAR(animator.playable().outputPose().local[0].translation.x, 2.5f, 1e-5f);

    ASSERT_EQ(animator.palette().size(), 1u);
    EXPECT_NEAR(animator.palette()[0][3].x, 2.5f, 1e-4f);
}

TEST(Hobi, PauseGraphFreezesTime) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiState* state = animator.play(clip);
    animator.update(0.25f);

    animator.playable().pauseGraph();
    animator.update(0.5f);
    EXPECT_NEAR(state->time(), 0.25f, 1e-5f);

    animator.playable().unpauseGraph();
    animator.update(0.25f);
    EXPECT_NEAR(state->time(), 0.5f, 1e-5f);
}

TEST(Hobi, TransitionAppliesSpeedAndStartTime) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f);

    ClipTransition transition;
    transition.clip = &clip;
    transition.fadeDurationValue = 0.0f;
    transition.speedValue = 2.0f;
    transition.normalizedStartTime = 0.5f;

    HobiState* state = animator.play(transition);
    ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->speed(), 2.0f);
    EXPECT_NEAR(state->normalizedTime(), 0.5f, 1e-5f);

    animator.update(0.25f);
    EXPECT_NEAR(state->time(), 1.0f, 1e-5f);
    EXPECT_NEAR(animator.playable().outputPose().local[0].translation.x, 10.0f, 1e-4f);
}

TEST(Hobi, EventsTriggerForwardAndEnd) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiState* state = animator.play(clip);

    EventSequence events;
    int general = 0;
    int end = 0;
    events.add(0.5f, [&general] { ++general; });
    events.endEvent() = HobiEvent(std::numeric_limits<float>::quiet_NaN(), [&end] { ++end; });
    state->setEvents(&events);

    animator.update(0.6f);
    EXPECT_EQ(general, 1);
    EXPECT_EQ(end, 0);

    animator.update(0.5f);  // time = 1.1, past the end
    EXPECT_EQ(end, 1);

    animator.update(0.1f);  // end event fires every frame after passing
    EXPECT_EQ(end, 2);
}

TEST(Hobi, LoopingEventFiresEachLoop) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f, 0.0f, 10.0f, true);
    HobiState* state = animator.play(clip);

    EventSequence events;
    int count = 0;
    events.add(0.25f, [&count] { ++count; });
    state->setEvents(&events);

    animator.update(0.3f);
    EXPECT_EQ(count, 1);

    animator.update(1.0f);  // crosses 1.25
    EXPECT_EQ(count, 2);
}

TEST(Hobi, EventsTriggerBackwards) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiState* state = animator.play(clip);

    state->setTimeD(1.0);
    state->setSpeed(-1.0f);

    EventSequence events;
    int count = 0;
    events.add(0.5f, [&count] { ++count; });
    state->setEvents(&events);

    animator.update(0.6f);  // 1.0 -> 0.4, crosses 0.5 backwards
    EXPECT_EQ(count, 1);
}

TEST(Hobi, StopClearsEvents) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiState* state = animator.play(clip);

    EventSequence events;
    events.add(0.5f, [] {});
    state->setEvents(&events);
    EXPECT_TRUE(state->hasEvents());

    state->stop();
    EXPECT_FALSE(state->hasEvents());
    EXPECT_TRUE(events.isEmpty());
}

TEST(Hobi, FacadePlayClipWithFadeMatchesLayer) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);

    HobiState* a = animator.play(clipA);
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(animator.isPlaying());
    EXPECT_TRUE(animator.isPlaying(clipA));
    EXPECT_FALSE(animator.isPlaying(clipB));

    HobiLayer* facadeLayer = animator.layer(0);
    ASSERT_NE(facadeLayer, nullptr);
    EXPECT_EQ(facadeLayer, animator.playable().layers().getLayer(0));

    HobiState* b = animator.play(clipB, 0.5f, FadeMode::FixedDuration);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(facadeLayer->currentState(), b);

    animator.update(0.25f);
    EXPECT_NEAR(b->weight(), 0.5f, 1e-5f);
    EXPECT_NEAR(a->weight(), 0.5f, 1e-5f);

    animator.update(0.25f);
    EXPECT_NEAR(b->weight(), 1.0f, 1e-5f);
    EXPECT_FALSE(a->isPlaying());
    EXPECT_TRUE(animator.isPlaying(clipB));
    EXPECT_FALSE(animator.isPlaying(clipA));

    HobiState* immediate = animator.play(clipA, 0.0f, FadeMode::FixedDuration);
    ASSERT_NE(immediate, nullptr);
    EXPECT_FLOAT_EQ(immediate->weight(), 1.0f);
}

TEST(Hobi, FacadePlayTransitionWithFade) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);

    HobiState* a = animator.play(clipA);
    ASSERT_NE(a, nullptr);

    ClipTransition transition;
    transition.clip = &clipB;
    transition.fadeDurationValue = 0.75f;

    HobiState* b = animator.play(transition, 0.5f, FadeMode::FixedDuration);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(animator.layer(0)->currentState(), b);

    animator.update(0.25f);
    EXPECT_NEAR(b->weight(), 0.5f, 1e-5f);
    EXPECT_NEAR(a->weight(), 0.5f, 1e-5f);

    animator.update(0.25f);
    EXPECT_NEAR(b->weight(), 1.0f, 1e-5f);
    EXPECT_FALSE(a->isPlaying());
}

TEST(Hobi, FacadeStopStopsAllLayers) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton, nullptr, 2);

    const BoneLocalClip clipA = makeLinearClip(1.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f);

    HobiLayer* layerA = animator.layer(0);
    HobiLayer* layerB = animator.layer(1);
    ASSERT_NE(layerA, nullptr);
    ASSERT_NE(layerB, nullptr);

    HobiState* a = layerA->play(clipA);
    HobiState* b = layerB->play(clipB);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(animator.isPlaying());

    animator.stop();
    EXPECT_FALSE(a->isPlaying());
    EXPECT_FALSE(b->isPlaying());
    EXPECT_FALSE(animator.isPlaying());
}

TEST(Hobi, FacadeIsSafeBeforeInitialize) {
    HobiAnimator animator;
    const BoneLocalClip clip = makeLinearClip(1.0f);

    EXPECT_FALSE(animator.isPlaying());
    EXPECT_FALSE(animator.isPlaying(clip));
    EXPECT_EQ(animator.layer(0), nullptr);
    EXPECT_EQ(animator.play(clip, 0.25f, FadeMode::FixedSpeed), nullptr);

    ClipTransition transition;
    transition.clip = &clip;
    EXPECT_EQ(animator.play(transition, 0.25f, FadeMode::FixedSpeed), nullptr);

    animator.stop();
}

TEST(Hobi, FacadePlayStateKeepsExistingLayer) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton, nullptr, 2);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiLayer* layer0 = animator.layer(0);
    HobiLayer* layer1 = animator.layer(1);
    ASSERT_NE(layer0, nullptr);
    ASSERT_NE(layer1, nullptr);

    HobiState* state = layer1->play(clip);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(layer1->currentState(), state);

    EXPECT_EQ(animator.play(state), state);
    EXPECT_EQ(layer1->currentState(), state);
    EXPECT_EQ(layer0->currentState(), nullptr);
}

TEST(Hobi, FacadePlayClipKeepsExistingLayer) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton, nullptr, 2);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiLayer* layer0 = animator.layer(0);
    HobiLayer* layer1 = animator.layer(1);
    ASSERT_NE(layer0, nullptr);
    ASSERT_NE(layer1, nullptr);

    HobiState* state = layer1->play(clip);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(layer1->currentState(), state);

    EXPECT_EQ(animator.play(clip), state);
    EXPECT_EQ(layer1->currentState(), state);
    EXPECT_EQ(layer0->currentState(), nullptr);

    EXPECT_EQ(animator.play(clip, 0.5f, FadeMode::FixedDuration), state);
    EXPECT_EQ(layer1->currentState(), state);
    EXPECT_EQ(layer0->currentState(), nullptr);
}

TEST(Hobi, LayerPlayClonesStateFromAnotherLayer) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton, nullptr, 2);

    const BoneLocalClip clip = makeLinearClip(10.0f, 0.0f, 10.0f);
    HobiLayer* layer0 = animator.layer(0);
    HobiLayer* layer1 = animator.layer(1);
    ASSERT_NE(layer0, nullptr);
    ASSERT_NE(layer1, nullptr);

    HobiState* source = layer1->play(clip);
    ASSERT_NE(source, nullptr);
    source->setSpeed(2.0f);
    source->setTimeD(3.0);

    HobiState* clone = layer0->play(source);
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone, source);
    EXPECT_EQ(clone->parent(), layer0);
    EXPECT_EQ(source->parent(), layer1);
    EXPECT_EQ(layer0->currentState(), clone);
    EXPECT_EQ(layer1->currentState(), source);
    EXPECT_FLOAT_EQ(clone->time(), 3.0f);
    EXPECT_FLOAT_EQ(clone->speed(), 2.0f);

    HobiState* fadeClone = layer1->play(clone, 0.5f, FadeMode::FixedDuration);
    ASSERT_NE(fadeClone, nullptr);
    EXPECT_NE(fadeClone, clone);
    EXPECT_EQ(fadeClone->parent(), layer1);
    EXPECT_EQ(layer1->currentState(), fadeClone);
}

TEST(Hobi, FacadePlayTransitionKeepsExistingLayer) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton, nullptr, 2);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiLayer* layer0 = animator.layer(0);
    HobiLayer* layer1 = animator.layer(1);
    ASSERT_NE(layer0, nullptr);
    ASSERT_NE(layer1, nullptr);

    ClipTransition transition;
    transition.clip = &clip;
    transition.fadeDurationValue = 0.0f;

    HobiState* state = layer1->play(transition);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(layer1->currentState(), state);

    EXPECT_EQ(animator.play(transition), state);
    EXPECT_EQ(layer1->currentState(), state);
    EXPECT_EQ(layer0->currentState(), nullptr);
}

TEST(Hobi, FacadeTryPlayAndStopKey) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    const Key key = Key::fromPointer(&clip);

    HobiState* state = animator.play(clip);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(animator.isPlaying(key));

    EXPECT_EQ(animator.stop(key), state);
    EXPECT_FALSE(animator.isPlaying(key));

    EXPECT_EQ(animator.tryPlay(key), state);
    EXPECT_TRUE(animator.isPlaying(key));

    EXPECT_EQ(animator.stop(clip), state);
    EXPECT_FALSE(animator.isPlayingClip(clip));
}

TEST(Hobi, FacadeTryPlayWithFade) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f);

    HobiState* a = animator.play(clipA);
    HobiState* b = animator.play(clipB);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_EQ(animator.tryPlay(Key::fromPointer(&clipA), 0.5f, FadeMode::FixedDuration), a);
    animator.update(0.25f);
    EXPECT_NEAR(a->weight(), 0.5f, 1e-5f);
    EXPECT_NEAR(b->weight(), 0.5f, 1e-5f);
    EXPECT_TRUE(animator.isPlayingClip(clipA));
    EXPECT_TRUE(animator.isPlayingClip(clipB));

    animator.update(0.25f);
    EXPECT_NEAR(a->weight(), 1.0f, 1e-5f);
    EXPECT_FALSE(b->isPlaying());
}

TEST(Hobi, FacadeEvaluateSpeedAndPause) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(10.0f, 0.0f, 10.0f);
    HobiState* state = animator.play(clip);
    ASSERT_NE(state, nullptr);

    animator.setSpeed(2.0f);
    EXPECT_FLOAT_EQ(animator.speed(), 2.0f);
    animator.evaluate(0.25f);
    EXPECT_NEAR(state->time(), 0.5f, 1e-5f);

    animator.evaluate();
    EXPECT_NEAR(state->time(), 0.5f, 1e-5f);

    EXPECT_TRUE(animator.isGraphPlaying());
    animator.pauseGraph();
    EXPECT_FALSE(animator.isGraphPlaying());
    animator.evaluate(0.5f);
    EXPECT_NEAR(state->time(), 0.5f, 1e-5f);

    animator.unpauseGraph();
    EXPECT_TRUE(animator.isGraphPlaying());
    animator.evaluate(0.5f);
    EXPECT_NEAR(state->time(), 1.5f, 1e-5f);
}

TEST(Hobi, FacadeConvenienceBeforeInitialize) {
    HobiAnimator animator;
    EXPECT_FALSE(animator.isInitialized());
    EXPECT_FLOAT_EQ(animator.speed(), 1.0f);
    EXPECT_FALSE(animator.isGraphPlaying());

    animator.evaluate();
    animator.evaluate(0.1f);
    animator.setSpeed(2.0f);
    animator.pauseGraph();
    animator.unpauseGraph();
}

TEST(Hobi, FacadePauseGraphStopsReportingPlaying) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeLinearClip(1.0f);
    HobiState* state = animator.play(clip);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(animator.isPlaying());
    EXPECT_TRUE(animator.isPlaying(clip));

    animator.playable().pauseGraph();
    EXPECT_FALSE(animator.isPlaying());
    EXPECT_FALSE(animator.isPlaying(clip));
    EXPECT_FALSE(animator.isPlayingClip(clip));

    animator.playable().unpauseGraph();
    EXPECT_TRUE(animator.isPlaying());
}

TEST(Hobi, RetargetClipPlaysOnDifferentSkeleton) {
    Skeleton sourceSkeleton;
    Transform sourceRest;
    sourceRest.translation = Vec3(0.0f, 1.0f, 0.0f);
    sourceSkeleton.addBone("Hips"_sid, -1, sourceRest);

    Skeleton targetSkeleton;
    targetSkeleton.addBone("Root"_sid, -1, Transform{});
    Transform targetRest;
    targetRest.translation = Vec3(0.0f, 2.0f, 0.0f);
    targetSkeleton.addBone("Hips"_sid, 0, targetRest);

    std::array<int32_t, kHumanBoneCount> sourceMap{};
    sourceMap.fill(-1);
    sourceMap[static_cast<std::size_t>(HumanBone::Hips)] = 0;
    std::array<int32_t, kHumanBoneCount> targetMap{};
    targetMap.fill(-1);
    targetMap[static_cast<std::size_t>(HumanBone::Hips)] = 1;

    const HumanoidAvatar sourceAvatar = createHumanoidAvatar(sourceSkeleton, sourceMap);
    const HumanoidAvatar targetAvatar = createHumanoidAvatar(targetSkeleton, targetMap);
    const BoneLocalClip clip = makeLinearClip(1.0f, 0.0f, 2.0f);

    ASSERT_EQ(clip.channels.size(), 1u);
    EXPECT_FLOAT_EQ(clip.length, 1.0f);
    EXPECT_FLOAT_EQ(clip.channels[0].position[0].evaluate(1.0f), 2.0f);

    Pose sourcePose;
    sourcePose.resizeLike(sourceSkeleton);
    sampleClip(clip, 0.5f, sourceSkeleton, nullptr, sourcePose);
    EXPECT_NEAR(sourcePose.local[0].translation.x, 1.0f, 1e-4f);
    sampleClip(clip, 1.0f, sourceSkeleton, nullptr, sourcePose);
    EXPECT_NEAR(sourcePose.local[0].translation.x, 2.0f, 1e-4f);
    Pose targetPose;
    targetPose.resizeLike(targetSkeleton);
    retargetHumanoidLocalPose(sourceAvatar, sourceSkeleton, sourcePose, targetAvatar,
                              targetSkeleton, targetPose);
    EXPECT_NEAR(targetPose.local[1].translation.x, 4.0f, 1e-4f);

    RetargetClipTransition transition;
    transition.clip = &clip;
    transition.sourceSkeleton = &sourceSkeleton;
    transition.sourceAvatar = &sourceAvatar;
    transition.targetAvatar = &targetAvatar;

    HobiAnimator animator;
    animator.initialize(targetSkeleton);
    HobiState* state = animator.play(transition);
    ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->length(), 1.0f);
    EXPECT_FALSE(state->isLooping());

    animator.update(0.5f);
    ASSERT_EQ(animator.playable().outputPose().local.size(), 2u);
    EXPECT_NEAR(animator.playable().outputPose().local[1].translation.x, 2.0f, 1e-4f);

    animator.update(0.5f);
    EXPECT_FLOAT_EQ(state->time(), 1.0f);
    PlayableNode* node = animator.playable().node(state->playable());
    ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(static_cast<float>(node->time()), 1.0f);
    EXPECT_NEAR(node->outputPose().local[1].translation.x, 4.0f, 1e-4f);
    EXPECT_NEAR(animator.playable().outputPose().local[1].translation.x, 4.0f, 1e-4f);

    RetargetClipTransition invalid;
    EXPECT_EQ(animator.play(invalid), nullptr);
}
