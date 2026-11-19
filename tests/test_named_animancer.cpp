#include <gtest/gtest.h>

#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/NamedAnimancerAnimator.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeNamedSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    return skeleton;
}

BoneLocalClip makeNamedClip(float length, float to) {
    BoneLocalClip clip;
    clip.length = length;

    BoneChannel channel;
    channel.boneIndex = 0;
    channel.hasPosition = 1;
    channel.position[0].keys = {
        Keyframe{0.0f, 0.0f, to / length, to / length},
        Keyframe{length, to, to / length, to / length},
    };
    clip.channels.push_back(channel);
    return clip;
}

}  // namespace

TEST(NamedAnimancerAnimator, RegistersAndResolvesNames) {
    Skeleton skeleton = makeNamedSkeleton();
    NamedAnimancerAnimator named;
    named.initialize(skeleton);

    const BoneLocalClip idle = makeNamedClip(1.0f, 10.0f);
    const BoneLocalClip walk = makeNamedClip(1.0f, 20.0f);

    named.setAnimations({{"Idle", &idle}, {"Walk", &walk}});

    ASSERT_EQ(named.animationNames().size(), 2u);
    EXPECT_EQ(named.findAnimation("Idle"), &idle);
    EXPECT_EQ(named.findAnimation("Walk"), &walk);
    EXPECT_EQ(named.findAnimation("Run"), nullptr);
    EXPECT_EQ(named.defaultAnimation(), &idle);
    EXPECT_TRUE(named.playAutomatically());

    EXPECT_TRUE(named.setDefaultAnimation("Walk"));
    EXPECT_EQ(named.defaultAnimation(), &walk);
    EXPECT_FALSE(named.setDefaultAnimation("Run"));
}

TEST(NamedAnimancerAnimator, PlaysAndStopsByName) {
    Skeleton skeleton = makeNamedSkeleton();
    NamedAnimancerAnimator named;
    named.initialize(skeleton);

    const BoneLocalClip idle = makeNamedClip(1.0f, 10.0f);
    const BoneLocalClip walk = makeNamedClip(1.0f, 20.0f);
    named.setAnimations({{"Idle", &idle}, {"Walk", &walk}});

    AnimancerState* state = named.play("Idle");
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(named.isPlaying("Idle"));
    EXPECT_FALSE(named.isPlaying("Walk"));

    EXPECT_EQ(named.stop("Idle"), state);
    EXPECT_FALSE(named.isPlaying("Idle"));
    EXPECT_EQ(named.play("Run"), nullptr);
    EXPECT_EQ(named.stop("Run"), nullptr);
}

TEST(NamedAnimancerAnimator, PlaysWithFadeAndDefault) {
    Skeleton skeleton = makeNamedSkeleton();
    NamedAnimancerAnimator named;
    named.initialize(skeleton);

    const BoneLocalClip idle = makeNamedClip(1.0f, 10.0f);
    const BoneLocalClip walk = makeNamedClip(1.0f, 20.0f);
    named.setAnimations({{"Idle", &idle}, {"Walk", &walk}});
    EXPECT_TRUE(named.setDefaultAnimation("Walk"));

    AnimancerState* idleState = named.play("Idle");
    AnimancerState* walkState = named.play("Walk", 0.5f, FadeMode::FixedDuration);
    ASSERT_NE(idleState, nullptr);
    ASSERT_NE(walkState, nullptr);

    named.update(0.25f);
    EXPECT_NEAR(idleState->weight(), 0.5f, 1e-5f);
    EXPECT_NEAR(walkState->weight(), 0.5f, 1e-5f);

    named.update(0.25f);
    EXPECT_TRUE(named.isPlaying("Walk"));
    EXPECT_FALSE(named.isPlaying("Idle"));

    EXPECT_EQ(named.playDefault(), walkState);
    EXPECT_EQ(named.start(), walkState);

    named.setPlayAutomatically(false);
    named.stop();
    EXPECT_EQ(named.start(), nullptr);
}

TEST(NamedAnimancerAnimator, HandlesInvalidAndGathersClips) {
    const BoneLocalClip idle = makeNamedClip(1.0f, 10.0f);
    const BoneLocalClip walk = makeNamedClip(1.0f, 20.0f);

    NamedAnimancerAnimator uninitialized;
    EXPECT_FALSE(uninitialized.isInitialized());
    EXPECT_EQ(uninitialized.play("Idle"), nullptr);
    uninitialized.update(0.1f);
    uninitialized.stop();

    Skeleton skeleton = makeNamedSkeleton();
    NamedAnimancerAnimator named;
    named.initialize(skeleton);
    EXPECT_FALSE(named.addAnimation("", idle));
    EXPECT_FALSE(named.removeAnimation("Idle"));

    ASSERT_TRUE(named.addAnimation("Idle", idle));
    ASSERT_TRUE(named.addAnimation("Idle", walk));
    ASSERT_EQ(named.animationNames().size(), 1u);
    EXPECT_EQ(named.findAnimation("Idle"), &walk);

    std::vector<const Clip*> clips;
    named.gatherAnimationClips(clips);
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_EQ(clips[0], &walk);

    EXPECT_TRUE(named.removeAnimation("Idle"));
    EXPECT_EQ(named.defaultAnimation(), nullptr);
}
