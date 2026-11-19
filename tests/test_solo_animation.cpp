#include <gtest/gtest.h>

#include "anim/hobi/SoloAnimation.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeSoloSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    return skeleton;
}

BoneLocalClip makeSoloClip(float length, float from, float to, bool looping = false) {
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

TEST(SoloAnimation, PlaysAndSamplesSingleClip) {
    Skeleton skeleton = makeSoloSkeleton();
    const BoneLocalClip clip = makeSoloClip(1.0f, 0.0f, 10.0f);

    SoloAnimation solo(skeleton);
    ASSERT_TRUE(solo.isInitialized());
    ASSERT_TRUE(solo.play(clip));
    EXPECT_TRUE(solo.isPlaying());

    solo.update(0.25f);
    EXPECT_NEAR(solo.time(), 0.25f, 1e-5f);
    EXPECT_NEAR(solo.pose().local[0].translation.x, 2.5f, 1e-5f);
    ASSERT_EQ(solo.palette().size(), 1u);
    EXPECT_NEAR(solo.palette()[0][3].x, 2.5f, 1e-4f);
}

TEST(SoloAnimation, PausesAndResumes) {
    Skeleton skeleton = makeSoloSkeleton();
    const BoneLocalClip clip = makeSoloClip(10.0f, 0.0f, 10.0f);

    SoloAnimation solo(skeleton);
    ASSERT_TRUE(solo.play(clip));
    solo.update(2.5f);
    EXPECT_NEAR(solo.time(), 2.5f, 1e-5f);

    solo.setIsPlaying(false);
    solo.update(1.0f);
    EXPECT_NEAR(solo.time(), 2.5f, 1e-5f);
    EXPECT_NEAR(solo.pose().local[0].translation.x, 2.5f, 1e-5f);

    solo.setIsPlaying(true);
    solo.update(2.5f);
    EXPECT_NEAR(solo.time(), 5.0f, 1e-5f);

    solo.setSpeed(0.0f);
    EXPECT_FALSE(solo.isPlaying());
    solo.update(1.0f);
    EXPECT_NEAR(solo.time(), 5.0f, 1e-5f);

    solo.setSpeed(2.0f);
    EXPECT_TRUE(solo.isPlaying());
    solo.update(1.0f);
    EXPECT_NEAR(solo.time(), 7.0f, 1e-5f);
}

TEST(SoloAnimation, LoopsAndStopsAtEnd) {
    Skeleton skeleton = makeSoloSkeleton();
    const BoneLocalClip looping = makeSoloClip(1.0f, 0.0f, 10.0f, true);
    const BoneLocalClip once = makeSoloClip(1.0f, 0.0f, 10.0f, false);

    SoloAnimation solo(skeleton);
    ASSERT_TRUE(solo.play(looping));
    solo.update(1.25f);
    EXPECT_NEAR(solo.time(), 1.25f, 1e-5f);
    EXPECT_NEAR(solo.normalizedTime(), 1.25f, 1e-5f);
    EXPECT_NEAR(solo.pose().local[0].translation.x, 2.5f, 1e-5f);
    EXPECT_TRUE(solo.isPlaying());

    ASSERT_TRUE(solo.play(once));
    solo.update(2.0f);
    EXPECT_NEAR(solo.time(), 1.0f, 1e-5f);
    EXPECT_NEAR(solo.pose().local[0].translation.x, 10.0f, 1e-5f);
    EXPECT_FALSE(solo.isPlaying());

    solo.update(1.0f);
    EXPECT_NEAR(solo.time(), 1.0f, 1e-5f);
}

TEST(SoloAnimation, TimeAndNormalizedTime) {
    Skeleton skeleton = makeSoloSkeleton();
    const BoneLocalClip clip = makeSoloClip(1.0f, 0.0f, 10.0f);

    SoloAnimation solo(skeleton);
    ASSERT_TRUE(solo.play(clip));

    solo.setTime(0.25f);
    solo.update(0.0f);
    EXPECT_NEAR(solo.normalizedTime(), 0.25f, 1e-5f);
    EXPECT_NEAR(solo.pose().local[0].translation.x, 2.5f, 1e-5f);

    solo.setNormalizedTime(0.75f);
    solo.update(0.0f);
    EXPECT_NEAR(solo.time(), 0.75f, 1e-5f);
    EXPECT_NEAR(solo.pose().local[0].translation.x, 7.5f, 1e-5f);
}

TEST(SoloAnimation, HandlesInvalidClipsAndUninitialized) {
    const BoneLocalClip clip = makeSoloClip(1.0f, 0.0f, 10.0f);

    SoloAnimation uninitialized;
    EXPECT_FALSE(uninitialized.isInitialized());
    EXPECT_FALSE(uninitialized.play(clip));
    EXPECT_FALSE(uninitialized.isPlaying());
    uninitialized.stop();
    uninitialized.update(1.0f);

    Skeleton skeleton = makeSoloSkeleton();
    SoloAnimation solo(skeleton);

    HumanoidClip humanoid;
    humanoid.length = 1.0f;
    EXPECT_FALSE(solo.play(humanoid));
    EXPECT_FALSE(solo.isPlaying());

    ASSERT_TRUE(solo.play(clip));
    std::vector<const Clip*> clips;
    solo.gatherAnimationClips(clips);
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_EQ(clips[0], &clip);

    solo.stop();
    EXPECT_FALSE(solo.isPlaying());
    EXPECT_NEAR(solo.time(), 0.0f, 1e-5f);
}
