#include <gtest/gtest.h>

#include <cmath>

#include "anim/clip/Clip.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"
#include "golden/spiderbot_data.h"

using namespace anim;

namespace {

void addConstantKey(Curve& curve, float value) {
    curve.keys = {Keyframe{0.0f, value, 0.0f, 0.0f}};
}

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());

    Transform hipsRest;
    hipsRest.translation = Vec3(9.0f, 9.0f, 9.0f);
    skeleton.addBone("Hips"_sid, 0, hipsRest);
    return skeleton;
}

BoneLocalClip makeClip() {
    BoneLocalClip clip;
    clip.length = 1.0f;
    clip.looping = true;

    BoneChannel channel;
    channel.boneIndex = 1;
    channel.hasPosition = 1;
    channel.hasRotation = 1;
    channel.hasScale = 1;

    // x: 0 -> 10 线性（切线 = 割线）。
    channel.position[0].keys = {
        Keyframe{0.0f, 0.0f, 10.0f, 10.0f},
        Keyframe{1.0f, 10.0f, 10.0f, 10.0f},
    };
    addConstantKey(channel.position[1], 2.0f);
    addConstantKey(channel.position[2], 3.0f);

    const Quat rotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Vec4 xyzw = quatToXYZW(rotation);
    for (int c = 0; c < 4; ++c) addConstantKey(channel.rotation[c], xyzw[c]);

    addConstantKey(channel.scale[0], 2.0f);
    addConstantKey(channel.scale[1], 2.0f);
    addConstantKey(channel.scale[2], 2.0f);

    clip.channels.push_back(channel);
    return clip;
}

}  // namespace

TEST(SampleClip, SamplesPositionRotationScaleAndResetsOthers) {
    const Skeleton skeleton = makeSkeleton();
    const BoneLocalClip clip = makeClip();

    Pose pose;
    pose.resizeLike(skeleton);
    // 故意写入垃圾，验证采样前会 resetToRest。
    pose[0].translation = Vec3(100.0f, 100.0f, 100.0f);
    pose[1].translation = Vec3(-1.0f, -1.0f, -1.0f);

    sampleClip(clip, 0.5f, skeleton, nullptr, pose);

    // 未出现的 Root 恢复绑定姿态。
    EXPECT_EQ(pose[0].translation, Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(std::abs(glm::dot(pose[0].rotation, kIdentityQuat)), 1.0f, 1e-6f);

    EXPECT_NEAR(pose[1].translation.x, 5.0f, 1e-5f);
    EXPECT_NEAR(pose[1].translation.y, 2.0f, 1e-5f);
    EXPECT_NEAR(pose[1].translation.z, 3.0f, 1e-5f);
    EXPECT_NEAR(glm::angle(pose[1].rotation), radians(90.0f), 1e-5f);
    EXPECT_NEAR(pose[1].scale.x, 2.0f, 1e-5f);

    // 同一 Pose 复用到另一时刻必须被覆盖而不是累加。
    sampleClip(clip, 0.0f, skeleton, nullptr, pose);
    EXPECT_NEAR(pose[1].translation.x, 0.0f, 1e-5f);
    sampleClip(clip, 1.0f, skeleton, nullptr, pose);
    EXPECT_NEAR(pose[1].translation.x, 10.0f, 1e-5f);
}

TEST(SampleClip, SkipsUnboundChannels) {
    Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip;

    BoneChannel unbound;
    unbound.boneIndex = -1;
    unbound.hasPosition = 1;
    unbound.position[0].keys = {Keyframe{0.0f, 42.0f, 0.0f, 0.0f}};
    clip.channels.push_back(unbound);

    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);
    sampleClip(clip, 0.0f, skeleton, nullptr, pose);

    EXPECT_EQ(pose[1].translation, Vec3(9.0f, 9.0f, 9.0f));
}

TEST(SampleClip, RejectsOutOfRangeBoneIndex) {
    Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip;

    BoneChannel bad;
    bad.boneIndex = 5;
    bad.hasScale = 1;
    clip.channels.push_back(bad);

    Pose pose;
    pose.resizeLike(skeleton);
    EXPECT_THROW(sampleClip(clip, 0.0f, skeleton, nullptr, pose), std::logic_error);
}

TEST(SampleClip, HumanoidClipThrowsUntilP4) {
    Skeleton skeleton = makeSkeleton();
    HumanoidClip clip;

    Pose pose;
    pose.resizeLike(skeleton);
    EXPECT_THROW(sampleClip(clip, 0.0f, skeleton, nullptr, pose), std::logic_error);
}

// 用 SpiderBot-MoveUp.anim 的真实 m_LocalRotation 关键帧
// （path: Root/RightUpperLeg）验证四分量曲线 -> 四元数 -> Pose 的链路。
TEST(SampleClip, MatchesSpiderBotRotationKeys) {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());

    BoneChannel channel;
    channel.boneIndex = 0;
    channel.hasRotation = 1;

    for (int c = 0; c < 4; ++c) {
        channel.rotation[c].preWrap = WrapMode::Loop;
        channel.rotation[c].postWrap = WrapMode::Loop;
    }

    for (int k = 0; k < test_golden::kSpiderBotRightUpperLegCount; ++k) {
        const test_golden::SpiderKey& key = test_golden::kSpiderBotRightUpperLeg[k];
        for (int c = 0; c < 4; ++c) {
            channel.rotation[c].keys.push_back(
                Keyframe{key.time, key.value[c], key.inSlope[c], key.outSlope[c]});
        }
    }

    BoneLocalClip clip;
    clip.length = test_golden::kSpiderBotRightUpperLeg[test_golden::kSpiderBotRightUpperLegCount - 1].time;
    clip.looping = true;
    clip.channels.push_back(channel);

    Pose pose;
    pose.resizeLike(skeleton);

    for (int k = 0; k < test_golden::kSpiderBotRightUpperLegCount; ++k) {
        const test_golden::SpiderKey& key = test_golden::kSpiderBotRightUpperLeg[k];
        sampleClip(clip, key.time, skeleton, nullptr, pose);

        const Quat expected = quatFromXYZW(key.value[0], key.value[1], key.value[2], key.value[3]);
        EXPECT_NEAR(std::abs(glm::dot(pose[0].rotation, safeNormalize(expected))), 1.0f, 1e-5f)
            << "key " << k;

        // 采样结果必须是单位四元数。
        EXPECT_NEAR(glm::length(pose[0].rotation), 1.0f, 1e-5f) << "key " << k;
    }
}
