#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "anim/avatar/HumanBone.h"
#include "anim/avatar/HumanoidAvatar.h"
#include "anim/avatar/HumanoidCalibration.h"
#include "anim/avatar/HumanoidIK.h"
#include "anim/avatar/MuscleSpace.h"
#include "anim/avatar/MuscleTable.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Palette.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeHumanoidSkeleton() {
    Skeleton skeleton;
    Transform hips;
    hips.translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.addBone("Hips"_sid, -1, hips);

    Transform spine;
    spine.translation = Vec3(0.0f, 0.2f, 0.0f);
    skeleton.addBone("Spine"_sid, 0, spine);

    return skeleton;
}

std::array<int32_t, kHumanBoneCount> makeBoneMap() {
    std::array<int32_t, kHumanBoneCount> map{};
    map.fill(-1);
    map[humanBoneIndex(HumanBone::Hips)] = 0;
    map[humanBoneIndex(HumanBone::Spine)] = 1;
    return map;
}

Curve constantCurve(float value) {
    Curve curve;
    curve.keys = {
        Keyframe{0.0f, value, 0.0f, 0.0f},
        Keyframe{1.0f, value, 0.0f, 0.0f},
    };
    return curve;
}

void expectQuatNear(const Quat& a, const Quat& b, float epsilon = 1e-5f) {
    EXPECT_NEAR(std::abs(glm::dot(a, b)), 1.0f, epsilon);
}

Transform boneWorld(const Skeleton& skeleton, const Pose& pose, int bone) {
    return Transform::fromMatrix(computeBoneWorldMatrix(skeleton, pose, bone));
}

Vec3 canonicalAxis(MuscleAxis axis) {
    switch (axis) {
        case MuscleAxis::X: return Vec3(1.0f, 0.0f, 0.0f);
        case MuscleAxis::Y: return Vec3(0.0f, 1.0f, 0.0f);
        case MuscleAxis::Z: return Vec3(0.0f, 0.0f, 1.0f);
    }
    return Vec3(1.0f, 0.0f, 0.0f);
}

}  // namespace

TEST(Humanoid, EulerXYZMatchesUnityAxesConversion) {
    const Vec3 euler(0.3f, -0.4f, 0.5f);
    const Quat q = detail::eulerXYZToQuat(euler);

    EXPECT_NEAR(q.w, 0.93159059f, 1e-6f);
    EXPECT_NEAR(q.x, 0.19050591f, 1e-6f);
    EXPECT_NEAR(q.y, -0.15409708f, 1e-6f);
    EXPECT_NEAR(q.z, 0.26851547f, 1e-6f);

    const Vec3 roundTrip = detail::quatToEulerXYZ(q);
    EXPECT_NEAR(roundTrip.x, euler.x, 1e-6f);
    EXPECT_NEAR(roundTrip.y, euler.y, 1e-6f);
    EXPECT_NEAR(roundTrip.z, euler.z, 1e-6f);

    const Quat gimbal = detail::eulerXYZToQuat(Vec3(0.2f, 0.5f * kPi, -0.3f));
    const Vec3 gimbalEuler = detail::quatToEulerXYZ(gimbal);
    EXPECT_NEAR(gimbalEuler.y, 0.5f * kPi, 1e-6f);
}

TEST(Humanoid, MuscleTableHas95UniqueNamedEntries) {
    EXPECT_EQ(kMuscleCount, 95);

    std::set<std::string> names;
    for (int i = 0; i < kMuscleCount; ++i) {
        const MuscleEntry& entry = muscleEntry(i);
        ASSERT_NE(entry.name, nullptr);
        names.insert(entry.name);
    }
    EXPECT_EQ(names.size(), static_cast<std::size_t>(kMuscleCount));

    EXPECT_STREQ(muscleEntry(0).name, "Spine Front-Back");
    EXPECT_STREQ(muscleEntry(94).name, "RightHand.Little.Spread");
    EXPECT_EQ(muscleEntry(24).kind, MuscleKind::Stretch);
    EXPECT_EQ(muscleEntry(24).bone, HumanBone::LeftLowerLeg);
    EXPECT_EQ(muscleEntry(41).axis, MuscleAxis::Z);
    EXPECT_EQ(findMuscleId("Left Arm Twist In-Out"), 41);
    EXPECT_EQ(findMuscleId("Not A Muscle"), -1);

    EXPECT_EQ(muscleEntry(24).defaultLimit.minDegrees, -30.0f);
    EXPECT_EQ(muscleEntry(24).defaultLimit.maxDegrees, 90.0f);
    EXPECT_EQ(muscleEntry(58).defaultLimit.minDegrees, -20.0f);

    EXPECT_STREQ(humanBoneName(HumanBone::UpperChest), "UpperChest");
    EXPECT_EQ(humanBoneIndex(HumanBone::UpperChest), 54);
}

TEST(Humanoid, AvatarMapsBonesAndCopiesRestPose) {
    Skeleton skeleton = makeHumanoidSkeleton();
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, makeBoneMap(), 1.0f);

    EXPECT_TRUE(avatar.hasBone(HumanBone::Hips));
    EXPECT_TRUE(avatar.hasBone(HumanBone::Spine));
    EXPECT_FALSE(avatar.hasBone(HumanBone::Head));
    EXPECT_EQ(avatar.boneIndex(HumanBone::Spine), 1);
    EXPECT_EQ(avatar.rest(HumanBone::Spine).translation, Vec3(0.0f, 0.2f, 0.0f));
    EXPECT_FLOAT_EQ(avatar.hipsHeight, 1.0f);

    // 未覆盖的 muscle 使用静态表默认 limits。
    EXPECT_EQ(avatar.limit(0).minDegrees, -90.0f);
    EXPECT_EQ(avatar.limit(24).maxDegrees, 90.0f);
}

TEST(Humanoid, ZeroMusclesProduceRestPose) {
    Skeleton skeleton = makeHumanoidSkeleton();
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, makeBoneMap());

    MuscleValues muscles;
    Pose pose;
    muscleSpaceToLocal(muscles, avatar, skeleton, pose);

    ASSERT_EQ(pose.local.size(), 2u);
    EXPECT_EQ(pose.local[0].translation, Vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(pose.local[1].translation, Vec3(0.0f, 0.2f, 0.0f));
    expectQuatNear(pose.local[0].rotation, kIdentityQuat);
    expectQuatNear(pose.local[1].rotation, kIdentityQuat);
}

TEST(Humanoid, SingleAxisUsesLimitsAndAxis) {
    Skeleton skeleton = makeHumanoidSkeleton();
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, makeBoneMap());
    avatar.setLimit(0, -30.0f, 60.0f);  // Spine Front-Back

    MuscleValues muscles;
    Pose pose;

    muscles[0] = 1.0f;
    muscleSpaceToLocal(muscles, avatar, skeleton, pose);
    expectQuatNear(pose.local[1].rotation,
                   glm::angleAxis(radians(60.0f), Vec3(1.0f, 0.0f, 0.0f)));

    muscles[0] = -1.0f;
    muscleSpaceToLocal(muscles, avatar, skeleton, pose);
    expectQuatNear(pose.local[1].rotation,
                   glm::angleAxis(radians(-30.0f), Vec3(1.0f, 0.0f, 0.0f)));

    muscles[0] = 0.5f;
    muscleSpaceToLocal(muscles, avatar, skeleton, pose);
    expectQuatNear(pose.local[1].rotation,
                   glm::angleAxis(radians(30.0f), Vec3(1.0f, 0.0f, 0.0f)));

    // 未映射的 Chest 不应影响任何骨骼。
    muscles[0] = 0.0f;
    muscles[3] = 1.0f;  // Chest Front-Back
    muscleSpaceToLocal(muscles, avatar, skeleton, pose);
    expectQuatNear(pose.local[1].rotation, kIdentityQuat);
}

TEST(Humanoid, MassCenterRootModeRoundTripsMassCenter) {
    Skeleton skeleton = makeHumanoidSkeleton();
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, makeBoneMap());

    Pose source;
    source.resetToRest(skeleton);
    source.local[0].translation = Vec3(2.0f, 3.0f, 4.0f);
    source.local[0].rotation =
        glm::angleAxis(radians(30.0f), Vec3(0.0f, 1.0f, 0.0f));

    const HumanoidSample sample = humanoidLocalToSample(
        avatar, skeleton, source, 1.0f, HumanoidRootMode::MassCenter);
    EXPECT_EQ(sample.rootMode, HumanoidRootMode::MassCenter);

    const Transform sourceMass =
        detail::humanoidMassCenterRoot(avatar, skeleton, source);
    EXPECT_GT(glm::distance(sample.rootT, source.local[0].translation), 1e-4f);
    EXPECT_NEAR(glm::distance(sample.rootT, sourceMass.translation), 0.0f,
                1e-5f);
    EXPECT_GT(std::abs(glm::dot(sample.rootQ, sourceMass.rotation)), 0.9999f);

    Pose target;
    target.resetToRest(skeleton);
    humanoidToLocal(sample, avatar, skeleton, target, 1.0f);

    const Transform targetMass =
        detail::humanoidMassCenterRoot(avatar, skeleton, target);
    EXPECT_NEAR(glm::distance(targetMass.translation, sample.rootT), 0.0f,
                1e-5f);
    EXPECT_GT(std::abs(glm::dot(targetMass.rotation, sample.rootQ)), 0.9999f);

    Skeleton rotatedSkeleton = makeHumanoidSkeleton();
    const Quat restRotation =
        glm::angleAxis(radians(30.0f), Vec3(0.0f, 1.0f, 0.0f));
    rotatedSkeleton.bones[0].restLocal.rotation = restRotation;
    HumanoidAvatar rotatedAvatar =
        createHumanoidAvatar(rotatedSkeleton, makeBoneMap());

    Pose rotatedRest;
    rotatedRest.resetToRest(rotatedSkeleton);
    const HumanoidSample restSample = humanoidLocalToSample(
        rotatedAvatar, rotatedSkeleton, rotatedRest, 1.0f,
        HumanoidRootMode::MassCenter);
    expectQuatNear(restSample.rootQ, kIdentityQuat);

    Pose rotatedPose = rotatedRest;
    const Quat extraRotation =
        glm::angleAxis(radians(20.0f), Vec3(0.0f, 1.0f, 0.0f));
    rotatedPose.local[0].rotation = restRotation * extraRotation;
    const HumanoidSample rotatedSample = humanoidLocalToSample(
        rotatedAvatar, rotatedSkeleton, rotatedPose, 1.0f,
        HumanoidRootMode::MassCenter);
    expectQuatNear(rotatedSample.rootQ, extraRotation);

    std::array<int32_t, kHumanBoneCount> parentedMap{};
    parentedMap.fill(-1);
    parentedMap[humanBoneIndex(HumanBone::Hips)] = 1;
    parentedMap[humanBoneIndex(HumanBone::Spine)] = 2;

    Skeleton parented;
    Transform armature;
    armature.translation = Vec3(5.0f, 0.0f, 0.0f);
    armature.rotation =
        glm::angleAxis(radians(25.0f), Vec3(0.0f, 1.0f, 0.0f));
    parented.addBone("Armature"_sid, -1, armature);
    parented.addBone("Hips"_sid, 0, Transform(Vec3(0.0f, 1.0f, 0.0f)));
    parented.addBone("Spine"_sid, 1, Transform(Vec3(0.0f, 0.2f, 0.0f)));
    HumanoidAvatar parentedAvatar =
        createHumanoidAvatar(parented, parentedMap);

    Pose parentedSource;
    parentedSource.resetToRest(parented);
    parentedSource.local[1].translation = Vec3(0.5f, 1.0f, 0.0f);
    parentedSource.local[1].rotation =
        glm::angleAxis(radians(40.0f), Vec3(0.0f, 0.0f, 1.0f));
    const HumanoidSample parentedSample = humanoidLocalToSample(
        parentedAvatar, parented, parentedSource, 1.0f,
        HumanoidRootMode::MassCenter);

    Pose parentedTarget;
    parentedTarget.resetToRest(parented);
    humanoidToLocal(parentedSample, parentedAvatar, parented, parentedTarget,
                    1.0f);

    const Transform parentedRoot = boneWorld(parented, parentedTarget, 0);
    EXPECT_NEAR(glm::distance(parentedRoot.translation, parentedSample.rootT),
                0.0f, 1e-5f);
    expectQuatNear(parentedRoot.rotation, parentedSample.rootQ);

    const Quat parentedReference =
        detail::humanoidMassCenterReferenceRotation(parentedAvatar, parented);
    const Transform parentedMass = detail::humanoidMassCenterRoot(
        parentedAvatar, parented, parentedTarget, parentedReference);
    EXPECT_NEAR(glm::distance(parentedMass.translation, parentedSample.rootT),
                0.0f, 1e-5f);
    expectQuatNear(parentedMass.rotation, parentedSample.rootQ);
}

TEST(Humanoid, MassCenterRootGoalsUseMassRoot) {
    Skeleton skeleton;
    skeleton.addBone("Hips"_sid, -1, Transform(Vec3(0.0f, 1.0f, 0.0f)));
    skeleton.addBone("LeftFoot"_sid, 0, Transform(Vec3(0.0f, -0.5f, 0.0f)));
    skeleton.addBone("LeftHand"_sid, 0, Transform(Vec3(0.5f, 0.0f, 0.0f)));

    std::array<int32_t, kHumanBoneCount> map{};
    map.fill(-1);
    map[humanBoneIndex(HumanBone::Hips)] = 0;
    map[humanBoneIndex(HumanBone::LeftFoot)] = 1;
    map[humanBoneIndex(HumanBone::LeftHand)] = 2;
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, map);

    Pose pose;
    pose.resetToRest(skeleton);
    pose.local[0].translation = Vec3(2.0f, 3.0f, 4.0f);

    const HumanoidSample sample = humanoidLocalToSample(
        avatar, skeleton, pose, 1.0f, HumanoidRootMode::MassCenter);
    const Transform massRoot =
        detail::humanoidMassCenterRoot(avatar, skeleton, pose);
    ASSERT_TRUE(sample.hasFootT);
    ASSERT_TRUE(sample.hasHandT);

    const Vec3 footWorld(2.0f, 2.5f, 4.0f);
    const Vec3 handWorld(2.5f, 3.0f, 4.0f);
    const Vec3 expectedFoot =
        glm::conjugate(massRoot.rotation) * (footWorld - massRoot.translation);
    const Vec3 expectedHand =
        glm::conjugate(massRoot.rotation) * (handWorld - massRoot.translation);
    EXPECT_NEAR(glm::distance(sample.leftFootT, expectedFoot), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(sample.leftHandT, expectedHand), 0.0f, 1e-5f);
}

TEST(Humanoid, RootTranslationAndRotationScaleWithAvatar) {
    Skeleton skeleton = makeHumanoidSkeleton();
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, makeBoneMap(), 1.0f);

    HumanoidSample sample;
    sample.hasRootT = true;
    sample.rootT = Vec3(1.0f, 2.0f, 3.0f);
    sample.hasRootQ = true;
    sample.rootQ = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));

    Pose pose;
    humanoidToLocal(sample, avatar, skeleton, pose, 2.0f);

    EXPECT_EQ(pose.local[0].translation, Vec3(1.0f, 2.0f, 3.0f) * 2.0f);
    expectQuatNear(pose.local[0].rotation, sample.rootQ);

    Transform rootTransform;
    rootTransform.translation = Vec3(4.0f, 5.0f, 6.0f);
    rootTransform.rotation =
        glm::angleAxis(radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));
    Pose posedWithRoot;
    humanoidToLocal(sample, avatar, skeleton, posedWithRoot, 2.0f, rootTransform);
    const Transform expectedRoot = Transform::compose(
        rootTransform, Transform{Vec3(2.0f, 4.0f, 6.0f), sample.rootQ, Vec3(1.0f)});
    EXPECT_NEAR(glm::distance(posedWithRoot.local[0].translation,
                              expectedRoot.translation),
                0.0f, 1e-5f);
    expectQuatNear(posedWithRoot.local[0].rotation, expectedRoot.rotation);
}

TEST(Humanoid, GoalDrivenTwoBoneIKReachesRootSpaceTarget) {
    Skeleton skeleton;
    Transform hips;
    hips.translation = Vec3(0.0f, 0.9f, 0.0f);
    skeleton.addBone("Hips"_sid, -1, hips);
    Transform upper;
    upper.translation = Vec3(0.15f, 0.0f, 0.0f);
    skeleton.addBone("LeftUpperLeg"_sid, 0, upper);
    Transform lower;
    lower.translation = Vec3(0.1f, -0.4f, 0.0f);
    skeleton.addBone("LeftLowerLeg"_sid, 1, lower);
    Transform foot;
    foot.translation = Vec3(0.0f, -0.4f, 0.0f);
    skeleton.addBone("LeftFoot"_sid, 2, foot);

    std::array<int32_t, kHumanBoneCount> map{};
    map.fill(-1);
    map[humanBoneIndex(HumanBone::Hips)] = 0;
    map[humanBoneIndex(HumanBone::LeftUpperLeg)] = 1;
    map[humanBoneIndex(HumanBone::LeftLowerLeg)] = 2;
    map[humanBoneIndex(HumanBone::LeftFoot)] = 3;
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, map);

    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);

    HumanoidSample sample;
    sample.hasFootT = true;
    sample.hasFootQ = true;
    sample.leftFootT = Vec3(0.25f, -0.35f, 0.1f);
    sample.leftFootQ = glm::angleAxis(radians(30.0f), Vec3(0.0f, 1.0f, 0.0f));

    HumanoidGoalWeights weights;
    weights.leftFootPosition = 1.0f;
    weights.leftFootRotation = 1.0f;
    applyHumanoidGoals(sample, avatar, skeleton, pose, weights);

    const Transform low = boneWorld(skeleton, pose, 3);
    const Vec3 expectedPosition = Vec3(0.25f, 0.55f, 0.1f);
    EXPECT_NEAR(glm::distance(low.translation, expectedPosition), 0.0f, 1e-3f)
        << "actual=(" << low.translation.x << "," << low.translation.y << ","
        << low.translation.z << ")";
    expectQuatNear(low.rotation, sample.leftFootQ, 1e-4f);

    // FullBodySolve passes the raw goal to Skeleton2BoneIK; only
    // Skeleton2BoneAdjustLength consumes the interpolated goal position.
    Pose partial;
    partial.resizeLike(skeleton);
    partial.resetToRest(skeleton);
    HumanoidGoalWeights halfWeights;
    halfWeights.leftFootPosition = 0.5f;
    applyHumanoidGoals(sample, avatar, skeleton, partial, halfWeights);
    const Vec3 partialPosition = boneWorld(skeleton, partial, 3).translation;
    const float partialDistance = glm::distance(partialPosition, expectedPosition);
    EXPECT_GT(partialDistance, 1e-3f);
    EXPECT_LT(partialDistance, glm::distance(Vec3(0.25f, 0.1f, 0.0f), expectedPosition));

    const Pose solved = pose;
    applyHumanoidGoals(sample, avatar, skeleton, pose, HumanoidGoalWeights{});
    for (std::size_t i = 0; i < pose.local.size(); ++i) {
        expectQuatNear(pose.local[i].rotation, solved.local[i].rotation);
        EXPECT_EQ(pose.local[i].translation, solved.local[i].translation);
    }

    // Without Skeleton Axes, SkeletonSetDoF falls back to qtan2Quat and
    // removes the mid-bone Y twist before the geometric solve.
    Pose twisted;
    twisted.resizeLike(skeleton);
    twisted.resetToRest(skeleton);
    twisted.local[2].rotation =
        glm::angleAxis(radians(45.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 twistedTarget =
        boneWorld(skeleton, twisted, 3).translation -
        boneWorld(skeleton, twisted, 0).translation;
    HumanoidSample twistedGoal;
    twistedGoal.hasFootT = true;
    twistedGoal.leftFootT = twistedTarget;
    HumanoidGoalWeights twistedWeights;
    twistedWeights.leftFootPosition = 1.0f;
    twistedWeights.legStretch = 0.0f;
    applyHumanoidGoals(twistedGoal, avatar, skeleton, twisted, twistedWeights);
    SCOPED_TRACE("mid DoF fallback");
    expectQuatNear(twisted.local[2].rotation, kIdentityQuat);

    // A target beyond the chain reach must trigger Unity's 5% length stretch.
    HumanoidSample far;
    far.hasFootT = true;
    far.leftFootT = Vec3(0.25f, -10.0f, 0.0f);
    HumanoidGoalWeights stretchWeights;
    stretchWeights.leftFootPosition = 1.0f;
    Pose stretched;
    stretched.resizeLike(skeleton);
    stretched.resetToRest(skeleton);
    applyHumanoidGoals(far, avatar, skeleton, stretched, stretchWeights);

    EXPECT_NEAR(glm::length(stretched.local[2].translation),
                glm::length(skeleton.restLocal(2).translation) * 1.05f, 1e-5f);
    EXPECT_NEAR(glm::length(stretched.local[3].translation),
                glm::length(skeleton.restLocal(3).translation) * 1.05f, 1e-5f);

    // TwistSolve(qtan fallback): scale the parent twist but keep the child
    // (foot) world rotation unchanged.
    Pose twistPose;
    twistPose.resizeLike(skeleton);
    twistPose.resetToRest(skeleton);
    twistPose.local[2].rotation =
        glm::angleAxis(radians(45.0f), Vec3(1.0f, 0.0f, 0.0f));
    const Quat childWorldBefore = boneWorld(skeleton, twistPose, 3).rotation;
    avatar.legTwist = 0.25f;
    applyHumanoidTwist(avatar, skeleton, twistPose);
    expectQuatNear(boneWorld(skeleton, twistPose, 3).rotation, childWorldBefore);
    const float expectedAngle =
        2.0f * std::atan(0.25f * std::tan(radians(22.5f)));
    expectQuatNear(twistPose.local[2].rotation,
                   glm::angleAxis(expectedAngle, Vec3(1.0f, 0.0f, 0.0f)));

    Skeleton collinear;
    collinear.addBone("Hips"_sid, -1, Transform{});
    const Transform segment(Vec3(0.0f, 1.0f, 0.0f));
    collinear.addBone("LeftUpperLeg"_sid, 0, segment);
    collinear.addBone("LeftLowerLeg"_sid, 1, segment);
    collinear.addBone("LeftFoot"_sid, 2, segment);
    std::array<int32_t, kHumanBoneCount> collinearMap{};
    collinearMap.fill(-1);
    collinearMap[humanBoneIndex(HumanBone::Hips)] = 0;
    collinearMap[humanBoneIndex(HumanBone::LeftUpperLeg)] = 1;
    collinearMap[humanBoneIndex(HumanBone::LeftLowerLeg)] = 2;
    collinearMap[humanBoneIndex(HumanBone::LeftFoot)] = 3;
    HumanoidAvatar collinearAvatar =
        createHumanoidAvatar(collinear, collinearMap);

    HumanoidSample collinearGoal;
    collinearGoal.hasFootT = true;
    collinearGoal.leftFootT = Vec3(1.0f, 0.0f, 0.0f);
    HumanoidGoalWeights collinearWeights;
    collinearWeights.leftFootPosition = 1.0f;
    collinearWeights.legStretch = 0.0f;
    Pose collinearPose;
    collinearPose.resizeLike(collinear);
    collinearPose.resetToRest(collinear);
    applyHumanoidGoals(collinearGoal, collinearAvatar, collinear,
                       collinearPose, collinearWeights);
    EXPECT_GT(boneWorld(collinear, collinearPose, 3).translation.x, 0.5f);

    Skeleton combined;
    combined.addBone("Hips"_sid, -1, Transform{});
    combined.addBone("LeftUpperLeg"_sid, 0, segment);
    combined.addBone("LeftLowerLeg"_sid, 1, segment);
    combined.addBone("LeftFoot"_sid, 2, segment);
    combined.addBone("Head"_sid, 0, Transform(Vec3(0.0f, 1.5f, 0.0f)));
    std::array<int32_t, kHumanBoneCount> combinedMap{};
    combinedMap.fill(-1);
    combinedMap[humanBoneIndex(HumanBone::Hips)] = 0;
    combinedMap[humanBoneIndex(HumanBone::LeftUpperLeg)] = 1;
    combinedMap[humanBoneIndex(HumanBone::LeftLowerLeg)] = 2;
    combinedMap[humanBoneIndex(HumanBone::LeftFoot)] = 3;
    combinedMap[humanBoneIndex(HumanBone::Head)] = 4;
    HumanoidAvatar combinedAvatar =
        createHumanoidAvatar(combined, combinedMap);

    Pose combinedPose;
    combinedPose.resetToRest(combined);
    const Vec3 lookTarget =
        boneWorld(combined, combinedPose, 4).translation + Vec3(0.0f, 0.0f, 1.0f);
    HumanoidLookAtWeights lookWeights;
    lookWeights.head = 1.0f;
    applyHumanoidFullBodySolve(collinearGoal, combinedAvatar, combined,
                               combinedPose, lookTarget, lookWeights,
                               collinearWeights);

    Pose sequentialPose;
    sequentialPose.resetToRest(combined);
    applyHumanoidLookAt(combinedAvatar, combined, sequentialPose, lookTarget,
                        lookWeights);
    applyHumanoidGoals(collinearGoal, combinedAvatar, combined,
                       sequentialPose, collinearWeights);

    for (std::size_t i = 0; i < combinedPose.local.size(); ++i) {
        expectQuatNear(combinedPose.local[i].rotation,
                       sequentialPose.local[i].rotation);
        EXPECT_EQ(combinedPose.local[i].translation,
                  sequentialPose.local[i].translation);
    }
}

TEST(Humanoid, LookAtTurnsHeadTowardWorldTarget) {
    Skeleton skeleton;
    Transform hips;
    skeleton.addBone("Hips"_sid, -1, hips);
    Transform spine;
    spine.translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.addBone("Spine"_sid, 0, spine);
    Transform chest;
    chest.translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.addBone("Chest"_sid, 1, chest);
    Transform neck;
    neck.translation = Vec3(0.0f, 0.2f, 0.0f);
    skeleton.addBone("Neck"_sid, 2, neck);
    Transform head;
    head.translation = Vec3(0.0f, 0.2f, 0.0f);
    skeleton.addBone("Head"_sid, 3, head);

    std::array<int32_t, kHumanBoneCount> map{};
    map.fill(-1);
    map[humanBoneIndex(HumanBone::Hips)] = 0;
    map[humanBoneIndex(HumanBone::Spine)] = 1;
    map[humanBoneIndex(HumanBone::Chest)] = 2;
    map[humanBoneIndex(HumanBone::Neck)] = 3;
    map[humanBoneIndex(HumanBone::Head)] = 4;
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, map);

    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);

    const Vec3 target =
        boneWorld(skeleton, pose, 4).translation + Vec3(0.0f, 0.0f, 1.0f);
    const Vec3 beforeDirection =
        glm::normalize(target - boneWorld(skeleton, pose, 4).translation);
    const Vec3 beforeForward =
        boneWorld(skeleton, pose, 4).rotation * Vec3(0.0f, 1.0f, 0.0f);

    HumanoidLookAtWeights weights;
    weights.body = 0.5f;
    weights.head = 1.0f;
    applyHumanoidLookAt(avatar, skeleton, pose, target, weights);

    const Vec3 afterDirection =
        glm::normalize(target - boneWorld(skeleton, pose, 4).translation);
    const Vec3 afterForward =
        boneWorld(skeleton, pose, 4).rotation * Vec3(0.0f, 1.0f, 0.0f);
    EXPECT_LT(glm::dot(beforeForward, beforeDirection), 0.2f);
    EXPECT_GT(glm::dot(afterForward, afterDirection), 0.8f);
}

TEST(Humanoid, LookAtBodyUsesHeadPivotNotEyeAverage) {
    Skeleton skeleton;
    skeleton.addBone("Hips"_sid, -1, Transform::identity());
    skeleton.addBone("Spine"_sid, 0, Transform(Vec3(0.0f, 1.0f, 0.0f)));
    skeleton.addBone("Chest"_sid, 1, Transform(Vec3(0.0f, 1.0f, 0.0f)));
    skeleton.addBone("Neck"_sid, 2, Transform(Vec3(0.0f, 0.2f, 0.0f)));
    skeleton.addBone("Head"_sid, 3, Transform(Vec3(0.0f, 0.2f, 0.0f)));
    skeleton.addBone("LeftEye"_sid, 4, Transform(Vec3(0.1f, 0.1f, 0.0f)));
    skeleton.addBone("RightEye"_sid, 4, Transform(Vec3(-0.1f, 0.1f, 0.0f)));

    std::array<int32_t, kHumanBoneCount> map{};
    map.fill(-1);
    map[humanBoneIndex(HumanBone::Hips)] = 0;
    map[humanBoneIndex(HumanBone::Spine)] = 1;
    map[humanBoneIndex(HumanBone::Chest)] = 2;
    map[humanBoneIndex(HumanBone::Neck)] = 3;
    map[humanBoneIndex(HumanBone::Head)] = 4;
    map[humanBoneIndex(HumanBone::LeftEye)] = 5;
    map[humanBoneIndex(HumanBone::RightEye)] = 6;
    HumanoidAvatar nearEyes = createHumanoidAvatar(skeleton, map);

    Skeleton farSkeleton = skeleton;
    farSkeleton.bones[5].restLocal.translation = Vec3(0.5f, 0.5f, 0.0f);
    farSkeleton.bones[6].restLocal.translation = Vec3(-0.5f, 0.5f, 0.0f);
    HumanoidAvatar farEyes = createHumanoidAvatar(farSkeleton, map);

    Pose nearPose;
    nearPose.resetToRest(skeleton);
    Pose farPose;
    farPose.resetToRest(farSkeleton);
    const Vec3 target =
        boneWorld(skeleton, nearPose, 4).translation + Vec3(0.0f, 0.0f, 1.0f);

    HumanoidLookAtWeights weights;
    weights.body = 1.0f;
    applyHumanoidLookAt(nearEyes, skeleton, nearPose, target, weights);
    applyHumanoidLookAt(farEyes, farSkeleton, farPose, target, weights);

    expectQuatNear(boneWorld(skeleton, nearPose, 1).rotation,
                   boneWorld(farSkeleton, farPose, 1).rotation);
    expectQuatNear(boneWorld(skeleton, nearPose, 2).rotation,
                   boneWorld(farSkeleton, farPose, 2).rotation);
}

TEST(Humanoid, LookAtEyesUsesSourceEyeDirection) {
    Skeleton skeleton;
    skeleton.addBone("Hips"_sid, -1, Transform::identity());
    skeleton.addBone("Spine"_sid, 0, Transform(Vec3(0.0f, 1.0f, 0.0f)));
    skeleton.addBone("Chest"_sid, 1, Transform(Vec3(0.0f, 1.0f, 0.0f)));
    skeleton.addBone("Neck"_sid, 2, Transform(Vec3(0.0f, 0.2f, 0.0f)));
    skeleton.addBone("Head"_sid, 3, Transform(Vec3(0.0f, 0.2f, 0.0f)));
    skeleton.addBone("LeftEye"_sid, 4, Transform(Vec3(0.1f, 0.0f, 0.0f)));
    skeleton.addBone("RightEye"_sid, 4, Transform(Vec3(-0.1f, 0.0f, 0.0f)));

    std::array<int32_t, kHumanBoneCount> map{};
    map.fill(-1);
    map[humanBoneIndex(HumanBone::Hips)] = 0;
    map[humanBoneIndex(HumanBone::Spine)] = 1;
    map[humanBoneIndex(HumanBone::Chest)] = 2;
    map[humanBoneIndex(HumanBone::Neck)] = 3;
    map[humanBoneIndex(HumanBone::Head)] = 4;
    map[humanBoneIndex(HumanBone::LeftEye)] = 5;
    map[humanBoneIndex(HumanBone::RightEye)] = 6;
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, map);

    Pose pose;
    pose.resetToRest(skeleton);
    const Vec3 target =
        boneWorld(skeleton, pose, 4).translation + Vec3(0.5f, 0.0f, 0.0f);

    HumanoidLookAtWeights weights;
    weights.eyes = 1.0f;
    applyHumanoidLookAt(avatar, skeleton, pose, target, weights);

    for (int eye = 5; eye <= 6; ++eye) {
        const Transform eyeWorld = boneWorld(skeleton, pose, eye);
        const Vec3 direction = glm::normalize(target - eyeWorld.translation);
        const Vec3 forward = eyeWorld.rotation * Vec3(1.0f, 0.0f, 0.0f);
        EXPECT_GT(glm::dot(forward, direction), 0.99f);
    }
}

TEST(Humanoid, SamplesHumanoidClipAndPose) {
    Skeleton skeleton = makeHumanoidSkeleton();
    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, makeBoneMap());
    avatar.setLimit(0, -30.0f, 60.0f);

    HumanoidClip clip;
    clip.length = 1.0f;
    clip.muscles.resize(kMuscleCount);
    clip.muscles[0] = constantCurve(1.0f);

    clip.hasRootT = true;
    clip.rootT[0] = constantCurve(0.5f);
    clip.rootT[1] = constantCurve(1.5f);
    clip.rootT[2] = constantCurve(-0.5f);

    const HumanoidSample sample = sampleHumanoidClip(clip, 0.25f);
    EXPECT_FLOAT_EQ(sample.muscles[0], 1.0f);
    EXPECT_FLOAT_EQ(sample.muscles[1], 0.0f);
    EXPECT_TRUE(sample.hasRootT);
    EXPECT_EQ(sample.rootT, Vec3(0.5f, 1.5f, -0.5f));

    Pose pose;
    pose.resizeLike(skeleton);
    sampleClip(clip, 0.25f, skeleton, &avatar, pose);
    ASSERT_EQ(pose.local.size(), 2u);
    expectQuatNear(pose.local[1].rotation,
                   glm::angleAxis(radians(60.0f), Vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_EQ(pose.local[0].translation, Vec3(0.5f, 1.5f, -0.5f));
}

TEST(Humanoid, CalibrationRoundTripsAll95Muscles) {
    Skeleton skeleton;
    std::array<int32_t, kHumanBoneCount> map{};

    for (int i = 0; i < kHumanBoneCount; ++i) {
        Transform rest;
        rest.translation = Vec3(0.0f, 0.1f * static_cast<float>(i), 0.0f);
        rest.rotation = safeNormalize(glm::angleAxis(
            radians(2.0f * static_cast<float>(i) + 3.0f),
            glm::normalize(Vec3(0.2f + 0.01f * static_cast<float>(i), 1.0f,
                                0.3f - 0.005f * static_cast<float>(i)))));
        const int index = skeleton.addBone(
            hashString(humanBoneName(static_cast<HumanBone>(i))), -1, rest);
        map[static_cast<std::size_t>(i)] = index;
    }

    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, map, 1.0f);
    HumanoidCalibrationData data;
    for (int i = 0; i < kHumanBoneCount; ++i) {
        data.hasRest[static_cast<std::size_t>(i)] = true;
        data.restRotation[static_cast<std::size_t>(i)] =
            avatar.rest(static_cast<HumanBone>(i)).rotation;
    }

    for (int i = 0; i < kHumanBoneCount; ++i) {
        const Quat pre = glm::normalize(glm::angleAxis(
            radians(15.0f + 1.5f * static_cast<float>(i)),
            glm::normalize(Vec3(0.4f, 1.0f, 0.2f + 0.01f * static_cast<float>(i)))));
        avatar.preRot[static_cast<std::size_t>(i)] = pre;
        avatar.postRot[static_cast<std::size_t>(i)] = glm::conjugate(pre);
    }

    for (int id = 0; id < kMuscleCount; ++id) {
        const MuscleEntry& entry = muscleEntry(id);
        const int bone = humanBoneIndex(entry.bone);
        const Quat rest = data.restRotation[static_cast<std::size_t>(bone)];
        const Quat pre = avatar.preRot[static_cast<std::size_t>(bone)];
        const Quat post = avatar.postRot[static_cast<std::size_t>(bone)];
        const MuscleLimit limit = avatar.limit(id);
        data.hasDefaultLimit[static_cast<std::size_t>(id)] = true;
        data.defaultLimits[static_cast<std::size_t>(id)] = limit;

        for (float value : {-1.0f, -0.5f, 0.5f, 1.0f}) {
            const float angle = value >= 0.0f ? value * limit.maxDegrees
                                              : value * (-limit.minDegrees);
            data.samples.push_back(MuscleCalibrationSample{
                id, value,
                safeNormalize(rest * pre *
                              glm::angleAxis(radians(angle), canonicalAxis(entry.axis)) *
                              post)});
        }
    }

    HumanoidCalibrationReport report;
    std::string error;
    ASSERT_TRUE(calibrateHumanoid(data, report, error)) << error;
    EXPECT_LT(report.maxErrorDegrees, 0.05f);
    applyHumanoidCalibration(avatar, report);

    MuscleValues muscles;
    for (int id = 0; id < kMuscleCount; ++id) {
        muscles[id] = 0.35f * std::sin(0.37f * static_cast<float>(id));
    }

    Pose pose;
    muscleSpaceToLocal(muscles, avatar, skeleton, pose);

    for (int bone = 0; bone < kHumanBoneCount; ++bone) {
        Quat delta = kIdentityQuat;
        for (int id = 0; id < kMuscleCount; ++id) {
            const MuscleEntry& entry = muscleEntry(id);
            if (humanBoneIndex(entry.bone) != bone) continue;
            const MuscleLimit limit = avatar.limit(id);
            const float value = std::clamp(muscles[id], -1.0f, 1.0f);
            const float angle = value >= 0.0f ? value * limit.maxDegrees
                                              : value * (-limit.minDegrees);
            delta = delta * glm::angleAxis(radians(angle), canonicalAxis(entry.axis));
        }

        const Quat expected = safeNormalize(
            avatar.rest(static_cast<HumanBone>(bone)).rotation *
            avatar.preRot[static_cast<std::size_t>(bone)] * delta *
            avatar.postRot[static_cast<std::size_t>(bone)]);
        expectQuatNear(pose.local[static_cast<std::size_t>(map[static_cast<std::size_t>(bone)])]
                           .rotation,
                       expected, 1e-4f);
    }
}
TEST(Humanoid, CalibrationSolvesPrePostAndLimits) {
    Skeleton skeleton = makeHumanoidSkeleton();
    const Quat rest = glm::normalize(
        glm::angleAxis(radians(18.0f), Vec3(0.3f, 1.0f, 0.1f)) *
        glm::angleAxis(radians(-12.0f), Vec3(1.0f, 0.0f, 0.4f)));
    skeleton.bones[1].restLocal.rotation = rest;

    HumanoidAvatar avatar = createHumanoidAvatar(skeleton, makeBoneMap());
    const int spine = humanBoneIndex(HumanBone::Spine);

    const Quat pre = glm::normalize(
        glm::angleAxis(radians(37.0f), glm::normalize(Vec3(0.2f, 0.9f, 0.3f))));
    const Quat post = glm::normalize(glm::conjugate(pre));

    HumanoidCalibrationData data;
    data.hasRest[static_cast<std::size_t>(spine)] = true;
    data.restRotation[static_cast<std::size_t>(spine)] = rest;
    data.hasDefaultLimit[24] = true;
    data.defaultLimits[24] = MuscleLimit{-30.0f, 90.0f};

    for (int id = 0; id < 3; ++id) {
        const MuscleEntry& entry = muscleEntry(id);
        const MuscleLimit limit = id == 0   ? MuscleLimit{-40.0f, 70.0f}
                                  : id == 1 ? MuscleLimit{-25.0f, 55.0f}
                                            : MuscleLimit{-60.0f, 45.0f};
        for (float value : {-1.0f, -0.5f, 0.5f, 1.0f}) {
            const float angle = value >= 0.0f ? value * limit.maxDegrees
                                              : value * (-limit.minDegrees);
            const Quat local = safeNormalize(
                rest * pre *
                glm::angleAxis(radians(angle), canonicalAxis(entry.axis)) * post);
            data.samples.push_back(
                MuscleCalibrationSample{id, value, local});
        }
    }

    HumanoidCalibrationReport report;
    std::string error;
    ASSERT_TRUE(calibrateHumanoid(data, report, error)) << error;
    EXPECT_LT(report.maxErrorDegrees, 0.05f);
    expectQuatNear(report.preRot[static_cast<std::size_t>(spine)], pre, 1e-4f);
    expectQuatNear(report.postRot[static_cast<std::size_t>(spine)], post, 1e-4f);
    EXPECT_NEAR(report.limits[0].minDegrees, -40.0f, 1e-4f);
    EXPECT_NEAR(report.limits[0].maxDegrees, 70.0f, 1e-4f);
    EXPECT_NEAR(report.limits[1].minDegrees, -25.0f, 1e-4f);
    EXPECT_NEAR(report.limits[1].maxDegrees, 55.0f, 1e-4f);
    EXPECT_NEAR(report.limits[2].minDegrees, -60.0f, 1e-4f);
    EXPECT_NEAR(report.limits[2].maxDegrees, 45.0f, 1e-4f);
    EXPECT_NEAR(report.limits[24].minDegrees, -30.0f, 1e-4f);
    EXPECT_NEAR(report.limits[24].maxDegrees, 90.0f, 1e-4f);

    // 只有 X/Y 两轴（如肩/手/脚）时，Z 轴由叉积补全。
    HumanoidCalibrationData twoAxis;
    twoAxis.hasRest = data.hasRest;
    twoAxis.restRotation = data.restRotation;
    for (const MuscleCalibrationSample& sample : data.samples) {
        if (sample.muscleId < 2) twoAxis.samples.push_back(sample);
    }
    HumanoidCalibrationReport twoAxisReport;
    ASSERT_TRUE(calibrateHumanoid(twoAxis, twoAxisReport, error)) << error;
    expectQuatNear(twoAxisReport.preRot[static_cast<std::size_t>(spine)], pre, 1e-4f);
    expectQuatNear(twoAxisReport.postRot[static_cast<std::size_t>(spine)], post, 1e-4f);

    applyHumanoidCalibration(avatar, report);
    MuscleValues muscles;
    muscles[0] = 0.5f;
    muscles[1] = -0.25f;
    Pose pose;
    muscleSpaceToLocal(muscles, avatar, skeleton, pose);

    const Quat expected = safeNormalize(
        rest * pre *
        glm::angleAxis(radians(0.5f * 70.0f), canonicalAxis(MuscleAxis::X)) *
        glm::angleAxis(radians(-0.25f * 25.0f), canonicalAxis(MuscleAxis::Y)) *
        post);
    expectQuatNear(pose.local[1].rotation, expected, 1e-4f);
}
