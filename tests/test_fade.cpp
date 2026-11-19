#include <gtest/gtest.h>

#include "anim/animancer/AnimancerLayer.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/LayerList.h"
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

TEST(AnimancerFade, WeightSetterClearsFade) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    AnimancerState* state = root.play(clip);
    ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->weight(), 1.0f);

    state->startFade(0.5f, 1.0f);
    EXPECT_FLOAT_EQ(state->targetWeight(), 0.5f);
    EXPECT_FLOAT_EQ(state->fadeSpeed(), 0.5f);

    state->setWeight(0.25f);
    EXPECT_FLOAT_EQ(state->weight(), 0.25f);
    EXPECT_FLOAT_EQ(state->targetWeight(), 0.25f);
    EXPECT_FLOAT_EQ(state->fadeSpeed(), 0.0f);
}

TEST(AnimancerFade, StartFadeZeroDurationIsInfiniteSpeed) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    AnimancerState* state = root.play(clip);

    state->startFade(0.0f, 0.0f);
    EXPECT_TRUE(std::isinf(state->fadeSpeed()));
    EXPECT_GT(state->fadeSpeed(), 0.0f);
}

TEST(AnimancerFade, EffectiveWeightMultipliesParents) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    AnimancerState* state = root.play(clip);

    root.layers().getLayer(0)->setWeight(0.5f);
    state->setWeight(0.5f);

    EXPECT_FLOAT_EQ(state->effectiveWeight(), 0.25f);
}

TEST(AnimancerFade, CrossFadeMovesWeightsOverTime) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);

    AnimancerLayer* layer = root.layers().getLayer(0);
    AnimancerState* a = layer->play(clipA);
    AnimancerState* b = layer->play(clipB, 0.5f, FadeMode::FixedDuration);

    ASSERT_NE(a, b);
    EXPECT_EQ(layer->currentState(), b);

    root.evaluate(0.25f);
    EXPECT_NEAR(b->weight(), 0.5f, 1e-5f);
    EXPECT_NEAR(a->weight(), 0.5f, 1e-5f);

    root.evaluate(0.25f);
    EXPECT_NEAR(b->weight(), 1.0f, 1e-5f);
    EXPECT_NEAR(a->weight(), 0.0f, 1e-5f);
    EXPECT_FALSE(a->isPlaying());
}

TEST(AnimancerFade, ZeroDurationPlayIsImmediate) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);

    AnimancerLayer* layer = root.layers().getLayer(0);
    layer->play(clipA);
    AnimancerState* b = layer->play(clipB, 0.0f, FadeMode::FixedDuration);

    EXPECT_FLOAT_EQ(b->weight(), 1.0f);
}

TEST(AnimancerFade, EffectiveSpeedMultipliesHierarchy) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    AnimancerState* state = root.play(clip);

    root.setSpeed(0.5f);
    root.layers().getLayer(0)->setSpeed(1.0f);
    state->setSpeed(2.0f);

    EXPECT_FLOAT_EQ(state->effectiveSpeed(), 1.0f);
}
