#include <gtest/gtest.h>

#include <cmath>

#include "anim/hobi/HobiAnimator.h"
#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/ClipState.h"
#include "anim/hobi/CustomFade.h"
#include "anim/hobi/Easing.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/MixerStates.h"
#include "anim/hobi/MixerTransitions.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    return skeleton;
}

BoneLocalClip makeLinearClip(float length, float from, float to, bool looping = false) {
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

TEST(Mixers, ManualMixerBlendsChildWeights) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);

    auto* mixer = new ManualMixerState(&animator.playable());
    auto* childA = mixer->add(clipA);
    auto* childB = mixer->add(clipB);

    childA->setWeight(0.25f);
    childB->setWeight(0.75f);

    animator.playable().play(mixer);
    animator.update(0.5f);

    EXPECT_EQ(mixer->childCount(), 2);
    EXPECT_NEAR(mixer->length(), 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(childA->weight(), 0.25f);
    EXPECT_FLOAT_EQ(childB->weight(), 0.75f);
    EXPECT_NEAR(childA->time(), 0.5f, 1e-5f);
    EXPECT_NEAR(childB->time(), 0.5f, 1e-5f);
    EXPECT_NEAR(animator.playable().outputPose().local[0].translation.x, 8.75f, 1e-4f);
}

TEST(Mixers, LinearMixerInterpolatesWeights) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);
    const BoneLocalClip clipC = makeLinearClip(1.0f, 0.0f, 30.0f);

    auto* mixer = new LinearMixerState(&animator.playable());
    auto* childA = mixer->add(clipA);
    auto* childB = mixer->add(clipB);
    auto* childC = mixer->add(clipC);

    mixer->setThresholds({0.0f, 0.5f, 1.0f});
    mixer->setParameter(0.75f);

    animator.playable().play(mixer);
    animator.update(0.0f);

    EXPECT_NEAR(childA->weight(), 0.0f, 1e-6f);
    EXPECT_NEAR(childB->weight(), 0.5f, 1e-6f);
    EXPECT_NEAR(childC->weight(), 0.5f, 1e-6f);
}

TEST(Mixers, LinearMixerExtrapolatesSpeed) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);

    auto* mixer = new LinearMixerState(&animator.playable());
    mixer->add(clipA);
    auto* childB = mixer->add(clipB);

    mixer->setThresholds({1.0f, 2.0f});
    mixer->setParameter(3.0f);

    animator.playable().play(mixer);
    animator.update(1.0f);

    EXPECT_NEAR(childB->weight(), 1.0f, 1e-6f);
    EXPECT_NEAR(mixer->playbackSpeedScale(), 1.5f, 1e-6f);
    EXPECT_NEAR(childB->time(), 1.5f, 1e-5f);
}

TEST(Mixers, DirectionalMixerWeightsSumToOne) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);
    const BoneLocalClip clipC = makeLinearClip(1.0f, 0.0f, 30.0f);

    auto* mixer = new DirectionalMixerState(&animator.playable());
    auto* childA = mixer->add(clipA);
    auto* childB = mixer->add(clipB);
    auto* childC = mixer->add(clipC);

    mixer->setThresholds({Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f), Vec2(-1.0f, 0.0f)});
    mixer->setParameter(Vec2(1.0f, 0.0f));

    animator.playable().play(mixer);
    animator.update(0.0f);

    EXPECT_NEAR(childA->weight(), 1.0f, 1e-4f);
    EXPECT_NEAR(childB->weight(), 0.0f, 1e-4f);
    EXPECT_NEAR(childC->weight(), 0.0f, 1e-4f);
}

TEST(Mixers, CartesianMixerWeightsSumToOne) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);
    const BoneLocalClip clipC = makeLinearClip(1.0f, 0.0f, 30.0f);

    auto* mixer = new CartesianMixerState(&animator.playable());
    auto* childA = mixer->add(clipA);
    auto* childB = mixer->add(clipB);
    auto* childC = mixer->add(clipC);

    mixer->setThresholds({Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f), Vec2(-1.0f, 0.0f)});
    mixer->setParameter(Vec2(1.0f, 0.0f));

    animator.playable().play(mixer);
    animator.update(0.0f);

    EXPECT_NEAR(childA->weight(), 1.0f, 1e-4f);
    EXPECT_NEAR(childB->weight(), 0.0f, 1e-4f);
    EXPECT_NEAR(childC->weight(), 0.0f, 1e-4f);
}

TEST(Mixers, LinearMixerTransitionInitializesState) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);
    const BoneLocalClip clipC = makeLinearClip(1.0f, 0.0f, 30.0f);

    LinearMixerTransition transition;
    transition.clips = {&clipA, &clipB, &clipC};
    transition.thresholds = {0.0f, 0.5f, 1.0f};
    transition.defaultParameter = 0.75f;
    transition.fadeDurationValue = 0.0f;

    HobiState* state = animator.play(transition);
    auto* mixer = dynamic_cast<LinearMixerState*>(state);
    ASSERT_NE(mixer, nullptr);
    EXPECT_EQ(mixer->childCount(), 3);
    EXPECT_NEAR(mixer->parameter(), 0.75f, 1e-6f);

    animator.update(0.0f);
    EXPECT_NEAR(mixer->getChild(0)->weight(), 0.0f, 1e-6f);
    EXPECT_NEAR(mixer->getChild(1)->weight(), 0.5f, 1e-6f);
    EXPECT_NEAR(mixer->getChild(2)->weight(), 0.5f, 1e-6f);
}

TEST(Mixers, MixerTransition2DCreatesDirectionalMixer) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);

    MixerTransition2D transition;
    transition.type = MixerTransition2D::Type::Directional;
    transition.clips = {&clipA, &clipB};
    transition.thresholds = {Vec2(1.0f, 0.0f), Vec2(-1.0f, 0.0f)};
    transition.defaultParameter = Vec2(1.0f, 0.0f);
    transition.fadeDurationValue = 0.0f;

    HobiState* state = animator.play(transition);
    auto* mixer = dynamic_cast<DirectionalMixerState*>(state);
    ASSERT_NE(mixer, nullptr);
    EXPECT_EQ(mixer->childCount(), 2);

    animator.update(0.0f);
    EXPECT_NEAR(mixer->getChild(0)->weight(), 1.0f, 1e-4f);
    EXPECT_NEAR(mixer->getChild(1)->weight(), 0.0f, 1e-4f);
}

TEST(Mixers, CustomFadeUsesCurve) {
    Skeleton skeleton = makeSkeleton();
    HobiAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clipA = makeLinearClip(1.0f, 0.0f, 10.0f);
    const BoneLocalClip clipB = makeLinearClip(1.0f, 0.0f, 20.0f);

    HobiState* stateA = animator.play(clipA);
    ASSERT_NE(stateA, nullptr);

    HobiLayer& layer = *animator.playable().layers()[0];
    HobiState* stateB = layer.play(clipB, 1.0f, FadeMode::FixedDuration);
    ASSERT_NE(stateB, nullptr);
    ASSERT_NE(stateA, stateB);

    CustomFade::apply(*stateB, [](float t) { return t * t; });

    animator.update(0.5f);
    EXPECT_NEAR(stateB->weight(), 0.25f, 1e-5f);
    EXPECT_NEAR(stateA->weight(), 0.75f, 1e-5f);

    animator.update(0.5f);
    EXPECT_NEAR(stateB->weight(), 1.0f, 1e-5f);
    EXPECT_NEAR(stateA->weight(), 0.0f, 1e-5f);
}
