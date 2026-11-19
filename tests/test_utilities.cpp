#include <gtest/gtest.h>

#include <cmath>

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/DontAllowFade.h"
#include "anim/hobi/ExitEvent.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/MixerParameterTween.h"
#include "anim/hobi/MixerStates.h"
#include "anim/hobi/TimeSynchronizationGroup.h"
#include "anim/hobi/TimeSynchronizer.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    return skeleton;
}

BoneLocalClip makeClip(float length) {
    BoneLocalClip clip;
    clip.length = length;
    return clip;
}

}  // namespace

TEST(TimeSynchronizer, StoresAndAppliesNormalizedTime) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(2.0f);
    const BoneLocalClip clipB = makeClip(2.0f);
    HobiState* stateA = root.play(clipA);

    stateA->setTime(0.5f);
    TimeSynchronizer<int> sync(1);
    sync.storeTime(stateA);
    EXPECT_DOUBLE_EQ(sync.normalizedTime, 0.25);

    HobiState* stateB = root.play(clipB);
    EXPECT_TRUE(sync.syncTime(stateB, 1, 0.0f));
    EXPECT_FLOAT_EQ(stateB->time(), 0.5f);
}

TEST(TimeSynchronizer, LayerOverloadsUseLayerCurrentState) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(2.0f);
    const BoneLocalClip clipB = makeClip(2.0f);
    HobiLayer* layer = root.layers().getLayer(0);
    HobiState* stateA = layer->play(clipA);
    stateA->setTime(0.5f);

    TimeSynchronizer<int> sync(1);
    sync.storeTime(layer);
    EXPECT_DOUBLE_EQ(sync.normalizedTime, 0.25);

    HobiState* stateB = layer->play(clipB);
    EXPECT_TRUE(sync.syncTime(layer, 1, 0.0f));
    EXPECT_FLOAT_EQ(stateB->time(), 0.5f);
}

TEST(TimeSynchronizer, GroupChangesSkipFirstSyncAndDefaultGroupIsOptional) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(2.0f);
    const BoneLocalClip clipB = makeClip(2.0f);
    const BoneLocalClip clipC = makeClip(2.0f);
    HobiState* stateA = root.play(clipA);
    stateA->setTime(0.5f);

    TimeSynchronizer<int> sync(1);
    sync.storeTime(stateA);

    HobiState* stateB = root.play(clipB);
    EXPECT_FALSE(sync.syncTime(stateB, 2, 0.0f));
    EXPECT_EQ(sync.currentGroup, 2);
    EXPECT_FLOAT_EQ(stateB->time(), 0.0f);

    stateB->setTime(0.8f);
    sync.storeTime(stateB);
    HobiState* stateC = root.play(clipC);
    EXPECT_TRUE(sync.syncTime(stateC, 2, 0.0f));
    EXPECT_NEAR(stateC->time(), 0.8f, 1e-5f);

    TimeSynchronizer<int> defaultGroup(0, false);
    defaultGroup.storeTime(stateC);
    EXPECT_FALSE(defaultGroup.syncTime(stateC, 0, 0.0f));

    TimeSynchronizer<int> synchronizedDefault(0, true);
    synchronizedDefault.storeTime(stateC);
    EXPECT_TRUE(synchronizedDefault.syncTime(stateC, 0, 0.0f));
}

TEST(TimeSynchronizationGroup, StoresAndSyncsSelectedKeys) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(2.0f);
    const BoneLocalClip clipB = makeClip(2.0f);
    HobiState* stateA = root.play(clipA);

    int keyA = 1;
    int keyB = 2;
    TimeSynchronizationGroup group(&root);
    group.add(&keyA);

    stateA->setTime(0.5f);
    EXPECT_TRUE(group.storeTime(&keyA, stateA));
    EXPECT_FLOAT_EQ(group.normalizedTime.value(), 0.25f);

    HobiState* stateB = root.play(clipB);
    EXPECT_TRUE(group.syncTime(&keyA, stateB, 0.0f));
    EXPECT_FLOAT_EQ(stateB->time(), 0.5f);
    EXPECT_FALSE(group.syncTime(&keyB, stateB, 0.0f));

    stateB->setTime(0.8f);
    group.add(&keyB);
    EXPECT_TRUE(group.storeTime(&keyB));
    EXPECT_FLOAT_EQ(group.normalizedTime.value(), 0.4f);

    group.setPlayable(nullptr);
    EXPECT_FALSE(group.normalizedTime.has_value());
}

TEST(MixerParameterTween, InterpolatesAndStopsAtTarget) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);
    auto* mixer = new LinearMixerState(&root);
    mixer->add(clipA);
    mixer->add(clipB);
    mixer->setThresholds({0.0f, 1.0f});
    mixer->setParameter(0.0f);
    root.play(mixer);

    MixerParameterTweenFloat tween(mixer);
    tween.start(1.0f, 1.0f);
    EXPECT_TRUE(tween.isActive());

    root.evaluate(0.5f);
    EXPECT_NEAR(mixer->parameter(), 0.5f, 1e-6f);
    EXPECT_TRUE(tween.isActive());

    root.evaluate(0.5f);
    EXPECT_FLOAT_EQ(mixer->parameter(), 1.0f);
    EXPECT_FALSE(tween.isActive());
}

TEST(MixerParameterTween, SupportsVector2AndExplicitStop) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);
    auto* mixer = new DirectionalMixerState(&root);
    mixer->add(clipA);
    mixer->add(clipB);
    mixer->setThresholds({Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f)});
    mixer->setParameter(Vec2(0.0f, 0.0f));
    root.play(mixer);

    MixerParameterTweenVector2 tween(mixer);
    tween.start(Vec2(2.0f, 4.0f), 2.0f);
    tween.setProgress(0.5f);
    root.evaluate(0.0f);
    EXPECT_NEAR(mixer->parameter().x, 1.0f, 1e-6f);
    EXPECT_NEAR(mixer->parameter().y, 2.0f, 1e-6f);

    tween.stop();
    EXPECT_FALSE(tween.isActive());
    root.evaluate(1.0f);
    EXPECT_NEAR(mixer->parameter().x, 1.0f, 1e-6f);
    EXPECT_NEAR(mixer->parameter().y, 2.0f, 1e-6f);
}

TEST(ExitEvent, FiresWhenEffectiveWeightBecomesZero) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    int calls = 0;

    ExitEvent* exit = ExitEvent::registerEvent(*state, [&] { ++calls; });
    ASSERT_NE(exit, nullptr);
    EXPECT_TRUE(exit->isActive());

    root.evaluate(0.25f);
    EXPECT_EQ(calls, 0);

    state->setWeight(0.0f);
    root.evaluate(0.25f);
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(exit->isActive());
}

TEST(ExitEvent, UnregisterPreventsCallback) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    int calls = 0;

    ExitEvent* exit = ExitEvent::registerEvent(*state, [&] { ++calls; });
    EXPECT_TRUE(ExitEvent::unregister(*state));
    EXPECT_FALSE(exit->isActive());

    state->setWeight(0.0f);
    root.evaluate(0.25f);
    EXPECT_EQ(calls, 0);
    EXPECT_FALSE(ExitEvent::unregister(*state));
}

TEST(ExitEvent, DestroyedNodeCancelsPendingEvent) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    int calls = 0;

    ExitEvent::registerEvent(*state, [&] { ++calls; });
    state->destroy();
    root.evaluate(0.25f);
    EXPECT_EQ(calls, 0);
}

TEST(ExitEvent, UnregisterFromPlayableRemovesOneAtATime) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);
    HobiState* stateA = root.play(clipA);
    HobiState* stateB = root.play(clipB);
    int calls = 0;

    ExitEvent::registerEvent(*stateA, [&] { ++calls; });
    ExitEvent::registerEvent(*stateB, [&] { ++calls; });

    EXPECT_TRUE(ExitEvent::unregister(root));
    EXPECT_TRUE(ExitEvent::unregister(root));
    EXPECT_FALSE(ExitEvent::unregister(root));

    stateA->setWeight(0.0f);
    stateB->setWeight(0.0f);
    root.evaluate(0.25f);
    EXPECT_EQ(calls, 0);
}

TEST(DontAllowFade, CancelsLayerAndCurrentStateFades) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);
    root.play(clipA);
    HobiState* stateB =
        root.layers().getLayer(0)->play(clipB, 1.0f, FadeMode::FixedDuration);
    ASSERT_GT(stateB->fadeSpeed(), 0.0f);

    HobiLayer* layer = root.layers().getLayer(0);
    layer->startFade(0.25f, 1.0f);

    DontAllowFade::enable(root);
    root.evaluate(0.0f);

    EXPECT_FLOAT_EQ(stateB->fadeSpeed(), 0.0f);
    EXPECT_FLOAT_EQ(stateB->weight(), 1.0f);
    EXPECT_FLOAT_EQ(layer->fadeSpeed(), 0.0f);
    EXPECT_FLOAT_EQ(layer->weight(), 0.25f);
}
