#include <gtest/gtest.h>

#include "anim/clip/Clip.h"

using namespace anim;

TEST(Clip, BoneChannelMasksAndDefaults) {
    BoneChannel channel;
    EXPECT_EQ(channel.boneIndex, -1);
    EXPECT_FALSE(channel.hasPosition);
    EXPECT_FALSE(channel.hasRotation);
    EXPECT_FALSE(channel.hasScale);

    channel.boneIndex = 3;
    channel.hasRotation = 1;
    EXPECT_EQ(channel.boneIndex, 3);
    EXPECT_TRUE(channel.hasRotation);

    channel.position[0].keys = {Keyframe{0.0f, 1.0f, 0.0f, 0.0f}};
    EXPECT_FLOAT_EQ(channel.position[0].evaluate(0.0f), 1.0f);
}

TEST(Clip, BoneLocalClipKeepsEventsAndLength) {
    BoneLocalClip clip;
    clip.length = 1.5f;
    clip.looping = true;
    clip.averageSpeed = 2.0f;
    clip.markers.push_back(EventMarker{0.5f, hashString("Footstep")});

    ASSERT_EQ(clip.type, ClipType::BoneLocal);
    EXPECT_FLOAT_EQ(clip.length, 1.5f);
    EXPECT_TRUE(clip.looping);
    ASSERT_EQ(clip.markers.size(), 1u);
    EXPECT_EQ(clip.markers[0].name, hashString("Footstep"));
}

TEST(Clip, HumanoidClipHasRootAndMuscleBuffers) {
    HumanoidClip clip;
    clip.muscles.resize(95);
    clip.rootT[0].keys = {Keyframe{0.0f, -0.036590192f, 0.0f, 0.0f}};
    clip.hasRootT = true;

    ASSERT_EQ(clip.type, ClipType::HumanoidMuscle);
    EXPECT_EQ(clip.muscles.size(), 95u);
    EXPECT_TRUE(clip.hasRootT);
    EXPECT_FALSE(clip.hasRootQ);
    EXPECT_FLOAT_EQ(clip.rootT[0].evaluate(0.0f), -0.036590192f);
}

TEST(Clip, FindsPropertyChannelsByNameAndPath) {
    BoneLocalClip clip;
    PropertyChannel rootProperty;
    rootProperty.attribute = hashString("FootFall");
    rootProperty.curve.keys = {Keyframe{0.0f, 1.0f, 0.0f, 0.0f}};
    clip.properties.push_back(rootProperty);

    PropertyChannel pathProperty;
    pathProperty.path = hashString("SpiderBot");
    pathProperty.attribute = hashString("Speed");
    clip.properties.push_back(pathProperty);

    const PropertyChannel* root = clip.findProperty("", "FootFall");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->path, kInvalidStringId);
    EXPECT_FLOAT_EQ(root->curve.evaluate(0.0f), 1.0f);

    EXPECT_NE(clip.findProperty("SpiderBot", "Speed"), nullptr);
    EXPECT_EQ(clip.findProperty("Other", "Speed"), nullptr);
    EXPECT_EQ(clip.findProperty("SpiderBot", "Missing"), nullptr);
}

