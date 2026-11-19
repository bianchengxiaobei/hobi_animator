#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "anim/skeleton/Blend.h"

using namespace anim;

namespace {

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    Transform hips;
    hips.translation = Vec3(1.0f, 2.0f, 3.0f);
    skeleton.addBone("Hips"_sid, 0, hips);
    return skeleton;
}

float quatAngle(const Quat& q) { return glm::angle(safeNormalize(q)); }

}  // namespace

TEST(Blend, TotalWeightAndNormalizeMatchOriginalSemantics) {
    std::vector<float> weights{1.0f, 3.0f};
    EXPECT_FLOAT_EQ(totalWeight(weights), 4.0f);

    normalizeWeights(weights);
    EXPECT_FLOAT_EQ(weights[0], 0.25f);
    EXPECT_FLOAT_EQ(weights[1], 0.75f);

    // total == 1 时直接返回（ManualMixerState.NormalizeWeights:1319）。
    std::vector<float> unit{0.25f, 0.75f};
    normalizeWeights(unit);
    EXPECT_FLOAT_EQ(unit[0], 0.25f);
    EXPECT_FLOAT_EQ(unit[1], 0.75f);
}

TEST(Blend, LerpPoseInterpolatesTranslationAndRotation) {
    const Skeleton skeleton = makeSkeleton();

    Pose a;
    Pose b;
    a.resizeLike(skeleton);
    b.resizeLike(skeleton);
    a[1].translation = Vec3(0.0f, 0.0f, 0.0f);
    a[1].rotation = kIdentityQuat;
    b[1].translation = Vec3(10.0f, 0.0f, 0.0f);
    b[1].rotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));

    Pose out;
    lerpPose(a, b, 0.5f, out);

    EXPECT_NEAR(out[1].translation.x, 5.0f, 1e-5f);
    EXPECT_NEAR(quatAngle(out[1].rotation), radians(45.0f), 1e-5f);

    // 45° 绕 Y：(1,0,0) -> (cos45, 0, -sin45)。
    const Vec3 rotated = out[1].rotation * Vec3(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(rotated.x, std::cos(radians(45.0f)), 1e-5f);
    EXPECT_NEAR(rotated.z, -std::sin(radians(45.0f)), 1e-5f);
}

TEST(Blend, AccumulateAndNormalizeAveragesPoses) {
    const Skeleton skeleton = makeSkeleton();

    Pose a;
    Pose b;
    a.resizeLike(skeleton);
    b.resizeLike(skeleton);
    a[1].translation = Vec3(2.0f, 0.0f, 0.0f);
    b[1].translation = Vec3(2.0f, 0.0f, 0.0f);

    Pose acc;
    resetAccumulator(skeleton, acc);
    accumulatePose(acc, a, 0.5f, &a);
    accumulatePose(acc, b, 0.5f, &a);
    normalizePose(acc);

    EXPECT_NEAR(acc[1].translation.x, 2.0f, 1e-5f);
    EXPECT_NEAR(std::abs(glm::dot(acc[1].rotation, kIdentityQuat)), 1.0f, 1e-6f);
}

TEST(Blend, HemisphereAlignmentPreventsCancellation) {
    const Skeleton skeleton = makeSkeleton();

    const Quat q = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));

    Pose a;
    Pose b;
    a.resizeLike(skeleton);
    b.resizeLike(skeleton);
    a[1].rotation = q;
    b[1].rotation = -q;  // 同一旋转的另一种表示

    Pose acc;
    resetAccumulator(skeleton, acc);
    accumulatePose(acc, a, 0.5f, &a);
    accumulatePose(acc, b, 0.5f, &a);
    normalizePose(acc);

    // 未做半球对齐时两者会抵消为 0；对齐后应仍是 q。
    EXPECT_NEAR(std::abs(glm::dot(acc[1].rotation, q)), 1.0f, 1e-5f);
}

TEST(Blend, BlendPosesSingleUnitWeightNormalizesCopy) {
    const Skeleton skeleton = makeSkeleton();

    Pose pose;
    pose.resizeLike(skeleton);
    pose[1].translation = Vec3(2.0f, 0.0f, 0.0f);
    pose[1].rotation = Quat(2.0f, 0.0f, 0.0f, 0.0f);

    std::vector<const Pose*> poses{&pose};
    std::vector<float> weights{1.0f};
    Pose out;
    blendPoses(poses, weights, out);

    EXPECT_EQ(out.local.size(), pose.local.size());
    EXPECT_NEAR(out[1].translation.x, 2.0f, 1e-5f);
    EXPECT_NEAR(glm::length(out[1].rotation), 1.0f, 1e-5f);
    EXPECT_NEAR(out[1].rotation.w, 1.0f, 1e-5f);
}

TEST(Blend, BlendPosesWeightsThreeWay) {
    const Skeleton skeleton = makeSkeleton();

    Pose p0;
    Pose p1;
    Pose p2;
    p0.resizeLike(skeleton);
    p1.resizeLike(skeleton);
    p2.resizeLike(skeleton);
    p0[1].translation = Vec3(0.0f, 0.0f, 0.0f);
    p1[1].translation = Vec3(10.0f, 0.0f, 0.0f);
    p2[1].translation = Vec3(20.0f, 0.0f, 0.0f);

    std::vector<const Pose*> poses{&p0, &p1, &p2};
    std::vector<float> weights{0.25f, 0.5f, 0.25f};

    Pose out;
    blendPoses(poses, weights, out);

    EXPECT_NEAR(out[1].translation.x, 10.0f, 1e-5f);
    EXPECT_NEAR(std::abs(glm::dot(out[1].rotation, kIdentityQuat)), 1.0f, 1e-6f);
}

TEST(Blend, AccumulateRejectsNegativeWeight) {
    const Skeleton skeleton = makeSkeleton();
    Pose pose;
    pose.resizeLike(skeleton);
    Pose acc;
    resetAccumulator(skeleton, acc);

    EXPECT_THROW(accumulatePose(acc, pose, -0.5f, nullptr), std::logic_error);
}
