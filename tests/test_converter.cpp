#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "AbWriter.h"
#include "AnimConverter.h"
#include "AvatarBuilder.h"
#include "AvatarLoader.h"
#include "FbxMetaParser.h"
#include "anim/integration/Json.h"
#include "SkeletonLoader.h"
#include "YamlAnimParser.h"
#include "anim/avatar/HumanoidIK.h"
#include "anim/avatar/MuscleSpace.h"
#include "anim/clip/ClipIO.h"
#include "anim/core/Hash.h"
#include "anim/skeleton/Palette.h"
#include "anim/skeleton/Pose.h"

using namespace anim;
using namespace anim::converter;

namespace {

std::string sourcePath(const std::string& relative) {
    return std::string(ANIM_ASSETS_DIR) + "/" + relative;
}

std::string humanoidWalkPath() {
    return sourcePath("Plugins/Animancer/Examples/Art/Humanoid Animations/Humanoid-Walk.anim");
}

std::string humanoidRunPath() {
    return sourcePath("Plugins/Animancer/Examples/Art/Humanoid Animations/Humanoid-Run.anim");
}

std::string humanoidIdlePath() {
    return sourcePath("Plugins/Animancer/Examples/Art/Humanoid Animations/Humanoid-Idle.anim");
}

std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::string spiderBotPath() {
    return sourcePath("Plugins/Animancer/Examples/Art/Spider Bot/SpiderBot-MoveUp.anim");
}

SkeletonAsset makeSpiderSkeleton() {
    SkeletonAsset asset;
    Transform root;
    asset.skeleton.addBone(hashString("Root"), -1, root);
    asset.boneNames.emplace_back("Root");

    Transform leg;
    asset.skeleton.addBone(hashString("RightUpperLeg"), 0, leg);
    asset.boneNames.emplace_back("RightUpperLeg");
    return asset;
}

}  // namespace

TEST(Converter, ParsesHumanoidWalk) {
    ParsedClip parsed;
    std::string error;
    ASSERT_TRUE(loadAnimationClipFile(humanoidWalkPath(), parsed, error)) << error;

    EXPECT_EQ(parsed.name, "Humanoid-Walk");
    EXPECT_FALSE(parsed.legacy);
    EXPECT_FALSE(parsed.compressed);
    EXPECT_TRUE(parsed.loopTime);
    EXPECT_NEAR(parsed.length(), 1.0416667f, 1e-5f);
    EXPECT_EQ(parsed.channels.size(), 130u);

    int muscles = 0;
    int rootCurves = 0;
    for (const ParsedCurveChannel& channel : parsed.channels) {
        ASSERT_TRUE(channel.hasFloat);
        if (findMuscleId(channel.attribute) >= 0)
            ++muscles;
        else
            ++rootCurves;
    }
    EXPECT_EQ(muscles, 95);
    EXPECT_EQ(rootCurves, 35);
}

TEST(Converter, RejectsCompressedAnimationClip) {
    const char* yaml = R"(AnimationClip:
  m_Name: CompressedTest
  m_Legacy: 0
  m_Compressed: 1
  m_AnimationClipSettings:
    m_StartTime: 0
    m_StopTime: 1
)";

    ParsedClip parsed;
    std::string error;
    ASSERT_TRUE(parseAnimationClip(yaml, parsed, error)) << error;
    EXPECT_EQ(parsed.name, "CompressedTest");
    EXPECT_FALSE(parsed.legacy);
    EXPECT_TRUE(parsed.compressed);

    const ConvertResult result = convertParsed(parsed, ConvertOptions{});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, "compressed AnimationClip is not supported");
    EXPECT_FALSE(result.clip);
    EXPECT_TRUE(result.warnings.empty());
}

TEST(Converter, ConvertsHumanoidAndRoundTrips) {
    ParsedClip parsed;
    std::string error;
    ASSERT_TRUE(loadAnimationClipFile(humanoidWalkPath(), parsed, error)) << error;

    ConvertOptions options;
    const ConvertResult result = convertParsed(parsed, options);
    ASSERT_TRUE(result.ok()) << result.error;
    ASSERT_EQ(result.clip->type, ClipType::HumanoidMuscle);

    const auto& humanoid = static_cast<const HumanoidClip&>(*result.clip);
    EXPECT_EQ(humanoid.muscles.size(), static_cast<std::size_t>(kMuscleCount));
    EXPECT_TRUE(humanoid.hasRootT);
    EXPECT_TRUE(humanoid.hasRootQ);
    EXPECT_FALSE(humanoid.muscles[0].empty());

    const std::vector<uint8_t> bytes = encodeClip(*result.clip, &error);
    ASSERT_FALSE(bytes.empty()) << error;
    const auto decoded = decodeClip(bytes, &error);
    ASSERT_NE(decoded, nullptr) << error;
    ASSERT_EQ(decoded->type, ClipType::HumanoidMuscle);

    const auto& copy = static_cast<const HumanoidClip&>(*decoded);
    for (float time : {0.0f, 0.25f, 0.5f, 1.0f}) {
        const HumanoidSample a = sampleHumanoidClip(humanoid, time);
        const HumanoidSample b = sampleHumanoidClip(copy, time);
        for (int i = 0; i < kMuscleCount; ++i) {
            EXPECT_NEAR(a.muscles[i], b.muscles[i], 1e-6f);
        }
        EXPECT_NEAR(a.rootT.x, b.rootT.x, 1e-6f);
        EXPECT_NEAR(a.rootQ.w, b.rootQ.w, 1e-6f);
        EXPECT_EQ(a.hasRootT, b.hasRootT);
    }
}

TEST(Converter, ConvertsBoneLocalWithSkeleton) {
    ParsedClip parsed;
    std::string error;
    ASSERT_TRUE(loadAnimationClipFile(spiderBotPath(), parsed, error)) << error;
    EXPECT_EQ(parsed.name, "SpiderBot-MoveUp");

    const SkeletonAsset asset = makeSpiderSkeleton();
    ConvertOptions options;
    options.skeleton = &asset;

    const ConvertResult result = convertParsed(parsed, options);
    ASSERT_TRUE(result.ok()) << result.error;
    ASSERT_EQ(result.clip->type, ClipType::BoneLocal);

    const auto& clip = static_cast<const BoneLocalClip&>(*result.clip);
    ASSERT_GE(clip.channels.size(), 2u);
    EXPECT_EQ(clip.channels[0].boneIndex, 0);
    EXPECT_TRUE(clip.channels[0].hasRotation);
    EXPECT_FALSE(clip.channels[0].rotation[0].empty());
    EXPECT_FALSE(result.warnings.empty());

    const std::vector<uint8_t> bytes = encodeClip(*result.clip, &error);
    ASSERT_FALSE(bytes.empty()) << error;
    const auto decoded = decodeClip(bytes, &error);
    ASSERT_NE(decoded, nullptr) << error;

    Pose poseA;
    Pose poseB;
    poseA.resizeLike(asset.skeleton);
    poseB.resizeLike(asset.skeleton);
    for (float time : {0.0f, 0.2f, 0.5f}) {
        sampleClip(clip, time, asset.skeleton, nullptr, poseA);
        sampleClip(*decoded, time, asset.skeleton, nullptr, poseB);
        EXPECT_NEAR(poseA.local[0].rotation.x, poseB.local[0].rotation.x, 1e-5f);
        EXPECT_NEAR(poseA.local[0].rotation.w, poseB.local[0].rotation.w, 1e-5f);
    }
}

TEST(Converter, ParsesFbxHumanMap) {
    FbxHumanMap map;
    std::string error;
    ASSERT_TRUE(parseFbxMetaFile(
        sourcePath("Plugins/Animancer/Examples/Art/Default Humanoid/"
                   "DefaultHumanoid.fbx.meta"),
        map, error))
        << error;

    EXPECT_GT(map.entries.size(), 50u);
    EXPECT_EQ(map.humanNameFor("Hips"), "Hips");
    EXPECT_EQ(map.humanNameFor("LeftUpLeg"), "LeftUpperLeg");
}

TEST(Converter, LoadsSkeletonAndAvatarJson) {
    const char* skeletonJson = R"({
        "bones": [
            {"name":"Root","parent":-1},
            {"name":"Hips","parent":0,"translation":[0,1,0],
             "rotation":[0,0,0,1],"scale":[1,1,1]}
        ]
    })";

    SkeletonAsset asset;
    std::string error;
    ASSERT_TRUE(loadSkeletonJson(skeletonJson, asset, error)) << error;
    ASSERT_EQ(asset.boneNames.size(), 2u);
    EXPECT_EQ(asset.resolvePath("Root/Hips"), 1);
    EXPECT_EQ(asset.resolvePath(""), 0);
    EXPECT_EQ(asset.pathOf(1), "Root/Hips");

    const char* avatarJson = R"({
        "name":"Test",
        "hipsHeight":1.25,
        "armTwist":0.4,
        "foreArmTwist":0.3,
        "upperLegTwist":0.2,
        "legTwist":0.1,
        "bones":{"Hips":"Hips","Spine":"Hips"},
        "limitsOverride":{"Left Lower Leg Stretch":[-40,80]}
    })";

    AvatarConfig avatar;
    ASSERT_TRUE(loadAvatarJson(avatarJson, avatar, error)) << error;
    EXPECT_EQ(avatar.name, "Test");
    EXPECT_FLOAT_EQ(avatar.hipsHeight, 1.25f);
    EXPECT_FLOAT_EQ(avatar.armTwist, 0.4f);
    EXPECT_FLOAT_EQ(avatar.foreArmTwist, 0.3f);
    EXPECT_FLOAT_EQ(avatar.upperLegTwist, 0.2f);
    EXPECT_FLOAT_EQ(avatar.legTwist, 0.1f);
    EXPECT_EQ(avatar.boneNames[static_cast<std::size_t>(HumanBone::Hips)], "Hips");
    ASSERT_EQ(avatar.limits.size(), 1u);
    EXPECT_FLOAT_EQ(avatar.limits[0].minDegrees, -40.0f);
    EXPECT_FLOAT_EQ(avatar.limits[0].maxDegrees, 80.0f);
}

TEST(Converter, LoadsCalibrationAndBuildsAvatar) {
    const char* skeletonJson = R"({
        "bones": [
            {"name":"Hips","parent":-1,"translation":[0,1,0],
             "rotation":[0,0,0,1],"scale":[1,1,1]},
            {"name":"Spine","parent":0,"translation":[0,0.2,0],
             "rotation":[0,0,0,1],"scale":[1,1,1]}
        ]
    })";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonJson(skeletonJson, skeleton, error)) << error;

    const char* avatarJson = R"({
        "name":"Calibrated",
        "hipsHeight":1.25,
        "armTwist":0.45,
        "foreArmTwist":0.35,
        "upperLegTwist":0.25,
        "legTwist":0.15,
        "bones":{"Hips":"Hips","Spine":"Spine"},
        "preRot":{"Spine":[0,0.70710678,0,0.70710678]},
        "postRot":{"Spine":[0,-0.70710678,0,0.70710678]},
        "limitsOverride":{"Spine Front-Back":[-15,35]}
    })";

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarJson(avatarJson, config, error)) << error;
    EXPECT_TRUE(config.hasPreRot[static_cast<std::size_t>(HumanBone::Spine)]);
    EXPECT_TRUE(config.hasPostRot[static_cast<std::size_t>(HumanBone::Spine)]);

    HumanoidAvatar avatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, avatar, error)) << error;
    EXPECT_EQ(avatar.boneIndex(HumanBone::Hips), 0);
    EXPECT_EQ(avatar.boneIndex(HumanBone::Spine), 1);
    EXPECT_FLOAT_EQ(avatar.hipsHeight, 1.25f);
    EXPECT_FLOAT_EQ(avatar.armTwist, 0.45f);
    EXPECT_FLOAT_EQ(avatar.foreArmTwist, 0.35f);
    EXPECT_FLOAT_EQ(avatar.upperLegTwist, 0.25f);
    EXPECT_FLOAT_EQ(avatar.legTwist, 0.15f);
    EXPECT_NEAR(std::abs(glm::dot(
                    avatar.pre(HumanBone::Spine),
                    quatFromXYZW(0.0f, 0.70710678f, 0.0f, 0.70710678f))),
                1.0f, 1e-5f);
    EXPECT_NEAR(std::abs(glm::dot(
                    avatar.post(HumanBone::Spine),
                    quatFromXYZW(0.0f, -0.70710678f, 0.0f, 0.70710678f))),
                1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(avatar.limit(0).minDegrees, -15.0f);
    EXPECT_FLOAT_EQ(avatar.limit(0).maxDegrees, 35.0f);
}
TEST(Converter, LoadsUnityUvwAndAppliesZyRoll) {
    const char* skeletonJson = R"({
        "bones": [
            {"name":"Hips","parent":-1,"translation":[0,1,0],
             "rotation":[0,0,0,1],"scale":[1,1,1]},
            {"name":"Spine","parent":0,"translation":[0,0.2,0],
             "rotation":[0,0,0,1],"scale":[1,1,1]}
        ]
    })";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonJson(skeletonJson, skeleton, error)) << error;

    const char* avatarJson = R"({
        "bones":{"Hips":"Hips","Spine":"Spine"},
        "uvw":{"Spine":[null,null,
            {"muscle":"Spine Front-Back","sign":-1,"min":-40,"max":40}]}
    })";

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarJson(avatarJson, config, error)) << error;
    EXPECT_TRUE(config.hasUvw[static_cast<std::size_t>(HumanBone::Spine)]);

    HumanoidAvatar avatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, avatar, error)) << error;
    EXPECT_TRUE(avatar.hasUvw[static_cast<std::size_t>(humanBoneIndex(HumanBone::Spine))]);

    MuscleValues muscles;
    muscles[0] = 0.5f;  // Spine Front-Back -> UVW.z, sign -1, 20 degrees.
    Pose pose;
    muscleSpaceToLocal(muscles, avatar, skeleton.skeleton, pose);

    EXPECT_NEAR(std::abs(glm::dot(
                    pose.local[1].rotation,
                    glm::angleAxis(radians(-20.0f), Vec3(0.0f, 0.0f, 1.0f)))),
                1.0f, 1e-5f);
}

TEST(Converter, LoadsOfflineUnityAxesAvatar) {
    const std::string root = std::string(ANIM_TESTS_DIR) + "/golden/";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonFile(root + "DefaultHumanoid.skeleton.json", skeleton,
                                 error))
        << error;

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarFile(root + "DefaultHumanoid.source.avatar.json", config,
                               error))
        << error;
    EXPECT_EQ(config.limits.size(), 92u);

    HumanoidAvatar avatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, avatar, error)) << error;

    int mapped = 0;
    for (int i = 0; i < kHumanBoneCount; ++i) {
        if (avatar.boneMap[static_cast<std::size_t>(i)] >= 0) ++mapped;
    }
    EXPECT_EQ(mapped, 54);

    const int spine = humanBoneIndex(HumanBone::Spine);
    EXPECT_TRUE(avatar.hasUvw[static_cast<std::size_t>(spine)]);
    EXPECT_EQ(avatar.uvw[static_cast<std::size_t>(spine)][2].muscleId, 0);
    ASSERT_TRUE(avatar.axes[static_cast<std::size_t>(spine)].valid);
    EXPECT_EQ(avatar.axes[static_cast<std::size_t>(spine)].type, 1);
    EXPECT_GT(avatar.axes[static_cast<std::size_t>(spine)].length, 0.0f);
    EXPECT_NEAR(std::abs(glm::dot(avatar.axes[static_cast<std::size_t>(spine)].preQ,
                                 config.axes[static_cast<std::size_t>(spine)].preQ)),
                1.0f, 1e-5f);
    EXPECT_NEAR(avatar.limit(0).minDegrees, -40.0f, 1e-4f);
    EXPECT_NEAR(avatar.limit(0).maxDegrees, 40.0f, 1e-4f);
    EXPECT_LT(std::abs(glm::dot(avatar.pre(HumanBone::Spine), kIdentityQuat)),
              0.9999f);

    MuscleValues muscles;
    for (float& value : muscles.values) value = 0.25f;
    Pose pose;
    muscleSpaceToLocal(muscles, avatar, skeleton.skeleton, pose);
    const int spineBone = avatar.boneIndex(HumanBone::Spine);
    ASSERT_GE(spineBone, 0);
    const Quat expected =
        quatFromXYZW(-0.0789287562f, 0.0982575291f, -0.0817007682f, 0.9886560038f);
    EXPECT_NEAR(std::abs(glm::dot(pose.local[static_cast<std::size_t>(spineBone)]
                                      .rotation,
                                  expected)),
                1.0f, 1e-4f);

    const int hipsBone = avatar.boneIndex(HumanBone::Hips);
    const int footBone = avatar.boneIndex(HumanBone::LeftFoot);
    ASSERT_GE(hipsBone, 0);
    ASSERT_GE(footBone, 0);
    const Transform rootTransform = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, pose, hipsBone));
    const Transform footTransform = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, pose, footBone));
    const Vec3 targetWorld = footTransform.translation + Vec3(0.02f, 0.02f, 0.0f);
    const HumanoidBoneAxes& footAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(HumanBone::LeftFoot))];
    ASSERT_TRUE(footAxes.valid);
    HumanoidSample goalSample;
    goalSample.hasFootT = true;
    goalSample.hasFootQ = true;
    goalSample.leftFootQ =
        glm::angleAxis(radians(20.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 goalBottomWorld =
        targetWorld +
        (rootTransform.rotation * goalSample.leftFootQ) *
            Vec3(footAxes.length, 0.0f, 0.0f);
    goalSample.leftFootT =
        glm::conjugate(rootTransform.rotation) *
        (goalBottomWorld - rootTransform.translation);
    HumanoidGoalWeights goalWeights;
    goalWeights.leftFootPosition = 1.0f;
    goalWeights.leftFootRotation = 1.0f;
    goalWeights.legStretch = 0.0f;
    applyHumanoidGoals(goalSample, avatar, skeleton.skeleton, pose, goalWeights);
    const Transform solvedFoot = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, pose, footBone));
    EXPECT_LT(glm::distance(solvedFoot.translation, targetWorld), 0.02f);
    const Quat expectedFoot = glm::normalize(
        rootTransform.rotation *
        glm::normalize(goalSample.leftFootQ * glm::conjugate(footAxes.postQ)));
    EXPECT_GT(std::abs(glm::dot(solvedFoot.rotation, expectedFoot)), 0.9999f);

    const int headBone = avatar.boneIndex(HumanBone::Head);
    ASSERT_GE(headBone, 0);
    const HumanoidBoneAxes& headAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(HumanBone::Head))];
    ASSERT_TRUE(headAxes.valid);
    const Transform headTransform = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, pose, headBone));
    const Quat sourceHead = glm::normalize(headTransform.rotation * headAxes.postQ);
    const Vec3 sourceForward = sourceHead * Vec3(0.0f, -1.0f, 0.0f);
    const Vec3 lookTarget =
        headTransform.translation +
        glm::normalize(sourceForward + Vec3(0.5f, 0.0f, 0.2f)) * 5.0f;
    HumanoidLookAtWeights lookWeights;
    lookWeights.head = 1.0f;
    applyHumanoidLookAt(avatar, skeleton.skeleton, pose, lookTarget, lookWeights);
    const Transform solvedHead = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, pose, headBone));
    const Vec3 solvedForward = glm::normalize(
        (glm::normalize(solvedHead.rotation * headAxes.postQ)) *
        Vec3(0.0f, -1.0f, 0.0f));
    const Vec3 targetDirection =
        glm::normalize(lookTarget - solvedHead.translation);
    EXPECT_GT(glm::dot(solvedForward, targetDirection), 0.9f);

    const int upperLegBone = avatar.boneIndex(HumanBone::LeftUpperLeg);
    const int lowerLegBone = avatar.boneIndex(HumanBone::LeftLowerLeg);
    ASSERT_GE(upperLegBone, 0);
    ASSERT_GE(lowerLegBone, 0);
    const HumanoidBoneAxes& lowerLegAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(HumanBone::LeftLowerLeg))];
    ASSERT_TRUE(lowerLegAxes.valid);
    pose.local[static_cast<std::size_t>(lowerLegBone)].rotation =
        glm::angleAxis(radians(35.0f), glm::normalize(Vec3(0.2f, 1.0f, 0.3f)));
    const Transform lowerBefore = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, pose, lowerLegBone));
    const Quat lowerLocalBefore =
        pose.local[static_cast<std::size_t>(lowerLegBone)].rotation;
    fixHumanoidMidDoF(avatar, skeleton.skeleton, pose, HumanBone::LeftUpperLeg,
                      HumanBone::LeftLowerLeg);
    const Transform lowerAfter = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, pose, lowerLegBone));
    EXPECT_GT(std::abs(glm::dot(lowerBefore.rotation, lowerAfter.rotation)), 0.9999f);

    Pose reference = pose;
    pose.local[static_cast<std::size_t>(lowerLegBone)].rotation =
        lowerLocalBefore * glm::angleAxis(radians(25.0f), Vec3(1.0f, 0.0f, 0.0f));
    alignHumanoidBoneToReference(avatar, skeleton.skeleton, pose, reference,
                                 HumanBone::LeftLowerLeg, HumanBone::LeftUpperLeg);
    const Quat referenceWorld = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, reference, lowerLegBone)).rotation;
    const Quat alignedWorld = Transform::fromMatrix(
        computeBoneWorldMatrix(skeleton.skeleton, pose, lowerLegBone)).rotation;
    const Vec3 referenceAxis =
        glm::normalize(referenceWorld * lowerLegAxes.postQ) * Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 alignedAxis =
        glm::normalize(alignedWorld * lowerLegAxes.postQ) * Vec3(1.0f, 0.0f, 0.0f);
    EXPECT_GT(glm::dot(referenceAxis, alignedAxis), 0.9999f);
}

TEST(Converter, CrossSkeletonRetargetPreservesRelativePose) {
    const std::string root = std::string(ANIM_TESTS_DIR) + "/golden/";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonFile(root + "DefaultHumanoid.skeleton.json", skeleton,
                                 error))
        << error;

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarFile(root + "DefaultHumanoid.source.avatar.json", config,
                               error))
        << error;

    HumanoidAvatar sourceAvatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, sourceAvatar, error)) << error;

    Skeleton other = skeleton.skeleton;
    for (std::size_t i = 0; i < other.bones.size(); ++i) {
        const float scale = 1.0f + 0.08f * static_cast<float>(i % 7);
        other.bones[i].restLocal.translation *= scale;
    }

    SkeletonAsset otherAsset;
    otherAsset.skeleton = other;
    otherAsset.boneNames = skeleton.boneNames;
    HumanoidAvatar otherAvatar;
    ASSERT_TRUE(buildHumanoidAvatar(otherAsset, config, otherAvatar, error)) << error;

    MuscleValues muscles;
    for (float& value : muscles.values) value = 0.35f;
    Pose sourcePose;
    Pose otherPose;
    muscleSpaceToLocal(muscles, sourceAvatar, skeleton.skeleton, sourcePose);
    muscleSpaceToLocal(muscles, otherAvatar, other, otherPose);

    std::vector<Mat4> sourceWorld;
    std::vector<Mat4> otherWorld;
    computeWorldMatrices(skeleton.skeleton, sourcePose, sourceWorld);
    computeWorldMatrices(other, otherPose, otherWorld);

    int comparedDirections = 0;
    for (int i = 0; i < kHumanBoneCount; ++i) {
        const HumanBone bone = static_cast<HumanBone>(i);
        const int sourceBone = sourceAvatar.boneIndex(bone);
        const int otherBone = otherAvatar.boneIndex(bone);
        if (sourceBone < 0 || otherBone < 0) continue;

        EXPECT_GT(std::abs(glm::dot(
                      sourcePose.local[static_cast<std::size_t>(sourceBone)].rotation,
                      otherPose.local[static_cast<std::size_t>(otherBone)].rotation)),
                  0.99999f);

        const int parent = skeleton.skeleton.bone(sourceBone).parent;
        if (parent < 0) continue;
        const Transform sourceChild = Transform::fromMatrix(sourceWorld[sourceBone]);
        const Transform sourceParent = Transform::fromMatrix(sourceWorld[parent]);
        const Transform otherChild = Transform::fromMatrix(otherWorld[otherBone]);
        const Transform otherParent =
            Transform::fromMatrix(otherWorld[other.bone(otherBone).parent]);
        const Vec3 sourceDirection = sourceChild.translation - sourceParent.translation;
        const Vec3 otherDirection = otherChild.translation - otherParent.translation;
        if (glm::length(sourceDirection) <= 1e-5f ||
            glm::length(otherDirection) <= 1e-5f) {
            continue;
        }
        EXPECT_GT(glm::dot(glm::normalize(sourceDirection),
                           glm::normalize(otherDirection)),
                  0.9999f);
        ++comparedDirections;
    }
    EXPECT_GT(comparedDirections, 0);
    float maxTranslationDifference = 0.0f;
    for (int i = 0; i < kHumanBoneCount; ++i) {
        const int sourceBone = sourceAvatar.boneIndex(static_cast<HumanBone>(i));
        const int otherBone = otherAvatar.boneIndex(static_cast<HumanBone>(i));
        if (sourceBone < 0 || otherBone < 0) continue;
        maxTranslationDifference =
            std::max(maxTranslationDifference,
                     glm::distance(sourcePose.local[static_cast<std::size_t>(sourceBone)].translation,
                                   otherPose.local[static_cast<std::size_t>(otherBone)].translation));
    }
    EXPECT_GT(maxTranslationDifference, 1e-4f);
}

TEST(Converter, OfflineUnityFullBodyMatchesClips) {
    const std::string root = std::string(ANIM_TESTS_DIR) + "/golden/";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonFile(root + "DefaultHumanoid.skeleton.json", skeleton,
                                 error))
        << error;

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarFile(root + "DefaultHumanoid.source.avatar.json", config,
                               error))
        << error;

    HumanoidAvatar avatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, avatar, error)) << error;

    struct Case {
        const char* name;
        std::string animPath;
        std::string fixture;
    };
    const Case cases[] = {
        {"Walk", humanoidWalkPath(), "DefaultHumanoid.walk.fullbody.json"},
        {"Run", humanoidRunPath(), "DefaultHumanoid.run.fullbody.json"},
        {"Idle", humanoidIdlePath(), "DefaultHumanoid.idle.fullbody.json"},
    };

    for (const Case& testCase : cases) {
        SCOPED_TRACE(testCase.name);

        json::Value expected;
        ASSERT_TRUE(json::parse(readTextFile(root + testCase.fixture), expected,
                                error))
            << error;
        const json::Value* samples = expected.find("samples");
        ASSERT_NE(samples, nullptr);
        ASSERT_TRUE(samples->isArray());
        ASSERT_FALSE(samples->array.empty());

        ParsedClip parsed;
        ASSERT_TRUE(loadAnimationClipFile(testCase.animPath, parsed, error)) << error;
        const ConvertResult converted = convertParsed(parsed, ConvertOptions{});
        ASSERT_TRUE(converted.ok()) << converted.error;
        const auto* clip = dynamic_cast<const HumanoidClip*>(converted.clip.get());
        ASSERT_NE(clip, nullptr);

        for (const json::Value& sampleValue : samples->array) {
            ASSERT_TRUE(sampleValue.isObject());
            const json::Value* timeValue = sampleValue.find("time");
            const json::Value* musclesValue = sampleValue.find("muscles");
            const json::Value* rootTValue = sampleValue.find("rootT");
            const json::Value* rootQValue = sampleValue.find("rootQ");
            const json::Value* goalsValue = sampleValue.find("goals");
            const json::Value* bonesValue = sampleValue.find("bones");
            ASSERT_NE(timeValue, nullptr);
            ASSERT_NE(musclesValue, nullptr);
            ASSERT_NE(rootTValue, nullptr);
            ASSERT_NE(rootQValue, nullptr);
            ASSERT_NE(goalsValue, nullptr);
            ASSERT_NE(bonesValue, nullptr);
            ASSERT_TRUE(timeValue->isNumber());
            ASSERT_TRUE(musclesValue->isObject());
            ASSERT_TRUE(rootTValue->isArray());
            ASSERT_TRUE(rootQValue->isArray());
            ASSERT_EQ(rootTValue->array.size(), 3u);
            ASSERT_EQ(rootQValue->array.size(), 4u);
            ASSERT_TRUE(goalsValue->isObject());
            ASSERT_TRUE(bonesValue->isObject());
            EXPECT_EQ(bonesValue->object.size(), 54u);

            const float time = static_cast<float>(timeValue->number);
            const HumanoidSample sample = sampleHumanoidClip(*clip, time);

            for (const auto& entry : musclesValue->object) {
                const int muscle = findMuscleId(entry.first);
                ASSERT_GE(muscle, 0) << entry.first;
                ASSERT_TRUE(entry.second.isNumber());
                EXPECT_NEAR(sample.muscles[muscle],
                            static_cast<float>(entry.second.number), 2e-4f)
                    << entry.first << " at t=" << time;
            }

            ASSERT_TRUE(sample.hasRootT);
            ASSERT_TRUE(sample.hasRootQ);
            for (int i = 0; i < 3; ++i) {
                ASSERT_TRUE(rootTValue->array[static_cast<std::size_t>(i)].isNumber());
                EXPECT_NEAR(sample.rootT[i],
                            static_cast<float>(
                                rootTValue->array[static_cast<std::size_t>(i)].number),
                            2e-4f)
                    << "RootT[" << i << "] at t=" << time;
            }
            for (int i = 0; i < 4; ++i) {
                ASSERT_TRUE(rootQValue->array[static_cast<std::size_t>(i)].isNumber());
            }
            const Quat expectedRootQ = quatFromXYZW(
                static_cast<float>(rootQValue->array[0].number),
                static_cast<float>(rootQValue->array[1].number),
                static_cast<float>(rootQValue->array[2].number),
                static_cast<float>(rootQValue->array[3].number));
            EXPECT_NEAR(std::abs(glm::dot(sample.rootQ, expectedRootQ)), 1.0f,
                        2e-4f)
                << "RootQ at t=" << time;

            ASSERT_TRUE(sample.hasFootT);
            ASSERT_TRUE(sample.hasFootQ);
            ASSERT_TRUE(sample.hasHandT);
            ASSERT_TRUE(sample.hasHandQ);

            const auto checkGoal = [&](const char* name, const Vec3& actualT,
                                       const Quat& actualQ) {
                const json::Value* goal = goalsValue->find(name);
                ASSERT_NE(goal, nullptr) << name;
                ASSERT_TRUE(goal->isObject());
                const json::Value* tValue = goal->find("t");
                const json::Value* qValue = goal->find("q");
                ASSERT_NE(tValue, nullptr) << name;
                ASSERT_NE(qValue, nullptr) << name;
                ASSERT_TRUE(tValue->isArray());
                ASSERT_TRUE(qValue->isArray());
                ASSERT_EQ(tValue->array.size(), 3u);
                ASSERT_EQ(qValue->array.size(), 4u);
                for (int i = 0; i < 3; ++i) {
                    EXPECT_NEAR(actualT[i],
                                static_cast<float>(tValue->array[static_cast<std::size_t>(i)].number),
                                2e-4f)
                        << name << ".T[" << i << "] at t=" << time;
                }
                const Quat expected = quatFromXYZW(
                    static_cast<float>(qValue->array[0].number),
                    static_cast<float>(qValue->array[1].number),
                    static_cast<float>(qValue->array[2].number),
                    static_cast<float>(qValue->array[3].number));
                EXPECT_NEAR(std::abs(glm::dot(actualQ, expected)), 1.0f, 2e-4f)
                    << name << ".Q at t=" << time;
            };
            checkGoal("LeftFoot", sample.leftFootT, sample.leftFootQ);
            checkGoal("RightFoot", sample.rightFootT, sample.rightFootQ);
            checkGoal("LeftHand", sample.leftHandT, sample.leftHandQ);
            checkGoal("RightHand", sample.rightHandT, sample.rightHandQ);

            Pose pose;
            humanoidToLocal(sample, avatar, skeleton.skeleton, pose);

            const int hipsBone = avatar.boneIndex(HumanBone::Hips);
            ASSERT_GE(hipsBone, 0);
            for (int i = 0; i < 3; ++i) {
                EXPECT_NEAR(
                    pose.local[static_cast<std::size_t>(hipsBone)].translation[i],
                    static_cast<float>(
                        rootTValue->array[static_cast<std::size_t>(i)].number),
                    2e-4f)
                    << "Hips translation[" << i << "] at t=" << time;
            }
            EXPECT_NEAR(std::abs(glm::dot(
                            pose.local[static_cast<std::size_t>(hipsBone)].rotation,
                            expectedRootQ)),
                        1.0f, 2e-4f)
                << "Hips root rotation at t=" << time;

            for (const auto& entry : bonesValue->object) {
                const HumanBone humanBone = humanBoneFromName(entry.first);
                ASSERT_NE(humanBone, HumanBone::Count) << entry.first;
                const int bone = avatar.boneIndex(humanBone);
                ASSERT_GE(bone, 0) << entry.first;
                ASSERT_TRUE(entry.second.isArray());
                ASSERT_EQ(entry.second.array.size(), 4u);
                ASSERT_TRUE(entry.second.array[0].isNumber());
                ASSERT_TRUE(entry.second.array[1].isNumber());
                ASSERT_TRUE(entry.second.array[2].isNumber());
                ASSERT_TRUE(entry.second.array[3].isNumber());

                const Quat expectedRotation = quatFromXYZW(
                    static_cast<float>(entry.second.array[0].number),
                    static_cast<float>(entry.second.array[1].number),
                    static_cast<float>(entry.second.array[2].number),
                    static_cast<float>(entry.second.array[3].number));
                const Quat actual =
                    pose.local[static_cast<std::size_t>(bone)].rotation;
                EXPECT_NEAR(std::abs(glm::dot(actual, expectedRotation)), 1.0f,
                            2e-4f)
                    << entry.first << " at t=" << time;
            }
        }
    }
}

TEST(Converter, RoundTripsMarkers) {
    BoneLocalClip clip;
    clip.length = 1.0f;
    EventMarker marker;
    marker.normalizedTime = 0.5f;
    marker.name = hashString("Footstep");
    clip.markers.push_back(marker);

    std::string error;
    const std::vector<uint8_t> bytes = encodeClip(clip, &error);
    ASSERT_FALSE(bytes.empty()) << error;
    const auto decoded = decodeClip(bytes, &error);
    ASSERT_NE(decoded, nullptr) << error;
    ASSERT_EQ(decoded->markers.size(), 1u);
    EXPECT_EQ(decoded->markers[0].name, marker.name);
    EXPECT_FLOAT_EQ(decoded->markers[0].normalizedTime, 0.5f);
}

TEST(Converter, ConvertsNonBoneFloatCurvesToProperties) {
    const char* yaml = R"(AnimationClip:
  m_Name: PropertyTest
  m_Legacy: 0
  m_Compressed: 0
  m_FloatCurves:
  - curve:
      serializedVersion: 2
      m_Curve:
      - serializedVersion: 3
        time: 0
        value: 0
        inSlope: 0
        outSlope: 0
        tangentMode: 0
        weightedMode: 0
        inWeight: 0.33333334
        outWeight: 0.33333334
      - serializedVersion: 3
        time: 1
        value: 1
        inSlope: 0
        outSlope: 0
        tangentMode: 0
        weightedMode: 0
        inWeight: 0.33333334
        outWeight: 0.33333334
      m_PreInfinity: 2
      m_PostInfinity: 2
    attribute: FootFall
    path: 
    classID: 95
  m_AnimationClipSettings:
    serializedVersion: 2
    m_StartTime: 0
    m_StopTime: 1
    m_LoopTime: 0
)";

    ParsedClip parsed;
    std::string error;
    ASSERT_TRUE(parseAnimationClip(yaml, parsed, error)) << error;
    ASSERT_EQ(parsed.channels.size(), 1u);

    const ConvertResult result = convertParsed(parsed, ConvertOptions{});
    ASSERT_TRUE(result.ok()) << result.error;
    ASSERT_EQ(result.clip->properties.size(), 1u);
    const PropertyChannel& property = result.clip->properties[0];
    EXPECT_EQ(property.path, kInvalidStringId);
    EXPECT_EQ(property.attribute, hashString("FootFall"));
    EXPECT_NEAR(property.curve.evaluate(0.5f), 0.5f, 1e-6f);

    const std::vector<uint8_t> bytes = encodeClip(*result.clip, &error);
    ASSERT_FALSE(bytes.empty()) << error;
    const auto decoded = decodeClip(bytes, &error);
    ASSERT_NE(decoded, nullptr) << error;
    ASSERT_EQ(decoded->properties.size(), 1u);
    EXPECT_EQ(decoded->properties[0].attribute, hashString("FootFall"));
    EXPECT_NEAR(decoded->properties[0].curve.evaluate(0.5f), 0.5f, 1e-6f);
}

TEST(Converter, ReadsLegacyV2Clip) {
    BoneLocalClip clip;
    clip.length = 1.0f;
    clip.markers.push_back(EventMarker{0.5f, hashString("Footstep")});

    std::string error;
    std::vector<uint8_t> bytes = encodeClip(clip, &error);
    ASSERT_FALSE(bytes.empty()) << error;
    ASSERT_GE(bytes.size(), 44u);

    // v3 header: magic(4) + version/type(4) + 8 u32 fields(32) = 40;
    // its propertyCount is the 9th u32. v2 has no such field.
    bytes[4] = 2;
    bytes[5] = 0;
    bytes.erase(bytes.begin() + 40, bytes.begin() + 44);

    const auto decoded = decodeClip(bytes, &error);
    ASSERT_NE(decoded, nullptr) << error;
    ASSERT_EQ(decoded->markers.size(), 1u);
    EXPECT_EQ(decoded->markers[0].name, hashString("Footstep"));
    EXPECT_TRUE(decoded->properties.empty());
}

TEST(Converter, RetargetPipelineAlignsEndpoints) {
    const std::string root = std::string(ANIM_TESTS_DIR) + "/golden/";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonFile(root + "DefaultHumanoid.skeleton.json", skeleton,
                                 error))
        << error;

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarFile(root + "DefaultHumanoid.source.avatar.json", config,
                               error))
        << error;

    HumanoidAvatar avatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, avatar, error)) << error;

    MuscleValues muscles;
    for (float& value : muscles.values) value = 0.2f;
    Pose pose;
    muscleSpaceToLocal(muscles, avatar, skeleton.skeleton, pose);
    const Pose reference = pose;

    const HumanBone perturbed[] = {
        HumanBone::LeftLowerArm, HumanBone::RightLowerArm,
        HumanBone::LeftLowerLeg, HumanBone::RightLowerLeg,
    };
    for (const HumanBone bone : perturbed) {
        const int index = avatar.boneIndex(bone);
        ASSERT_GE(index, 0);
        pose.local[static_cast<std::size_t>(index)].rotation =
            glm::angleAxis(radians(30.0f), Vec3(0.0f, 0.0f, 1.0f)) *
            pose.local[static_cast<std::size_t>(index)].rotation;
    }

    retargetHumanoidPose(avatar, skeleton.skeleton, pose, reference);

    const HumanBone endpoints[] = {
        HumanBone::LeftHand, HumanBone::RightHand,
        HumanBone::LeftFoot, HumanBone::RightFoot,
    };
    for (const HumanBone bone : endpoints) {
        const int index = avatar.boneIndex(bone);
        ASSERT_GE(index, 0);
        const Quat expected = Transform::fromMatrix(
            computeBoneWorldMatrix(skeleton.skeleton, reference, index)).rotation;
        const Quat actual = Transform::fromMatrix(
            computeBoneWorldMatrix(skeleton.skeleton, pose, index)).rotation;
        EXPECT_GT(std::abs(glm::dot(expected, actual)), 0.9999f);
    }
}


TEST(Converter, LocalToHumanoidSampleRoundTripsMuscles) {
    const std::string root = std::string(ANIM_TESTS_DIR) + "/golden/";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonFile(root + "DefaultHumanoid.skeleton.json", skeleton,
                                 error))
        << error;

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarFile(root + "DefaultHumanoid.source.avatar.json", config,
                               error))
        << error;

    HumanoidAvatar avatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, avatar, error)) << error;

    MuscleValues muscles;
    for (int i = 0; i < kMuscleCount; ++i) {
        muscles[i] = 0.75f * std::sin(static_cast<float>(i) * 0.37f);
    }
    Pose pose;
    muscleSpaceToLocal(muscles, avatar, skeleton.skeleton, pose);
    const HumanoidSample sample =
        humanoidLocalToSample(avatar, skeleton.skeleton, pose);

    bool bound[kMuscleCount] = {};
    for (int i = 0; i < kHumanBoneCount; ++i) {
        for (int slot = 0; slot < 3; ++slot) {
            const int id = avatar.uvw[static_cast<std::size_t>(i)]
                                      [static_cast<std::size_t>(slot)]
                                          .muscleId;
            if (id >= 0) bound[id] = true;
        }
    }

    int compared = 0;
    for (int i = 0; i < kMuscleCount; ++i) {
        if (!bound[i]) continue;
        const MuscleLimit limit = avatar.limit(i);
        if (limit.minDegrees == 0.0f && limit.maxDegrees == 0.0f) continue;
        EXPECT_NEAR(sample.muscles[i], muscles[i], 2e-3f)
            << "muscle id " << i;
        ++compared;
    }
    EXPECT_GT(compared, 50);

    const int hips = avatar.boneIndex(HumanBone::Hips);
    ASSERT_GE(hips, 0);
    EXPECT_NEAR(glm::distance(sample.rootT,
                              pose.local[static_cast<std::size_t>(hips)].translation),
                0.0f, 1e-5f);
    EXPECT_GT(std::abs(glm::dot(
                  sample.rootQ,
                  pose.local[static_cast<std::size_t>(hips)].rotation)),
              0.99999f);

    EXPECT_TRUE(sample.hasFootT);
    EXPECT_TRUE(sample.hasFootQ);
    EXPECT_TRUE(sample.hasHandT);
    EXPECT_TRUE(sample.hasHandQ);

    Pose goalPose = pose;
    HumanoidGoalWeights goalWeights;
    goalWeights.leftFootPosition = 1.0f;
    goalWeights.leftFootRotation = 1.0f;
    applyHumanoidGoals(sample, avatar, skeleton.skeleton, goalPose, goalWeights);
    const HumanoidSample roundTrip =
        humanoidLocalToSample(avatar, skeleton.skeleton, goalPose);
    EXPECT_NEAR(glm::distance(roundTrip.leftFootT, sample.leftFootT), 0.0f, 2e-2f);
    EXPECT_GT(std::abs(glm::dot(roundTrip.leftFootQ, sample.leftFootQ)), 0.9999f);

    Skeleton partial;
    partial.addBone("Hips"_sid, -1, Transform(Vec3(0.0f, 1.0f, 0.0f)));
    partial.addBone("Head"_sid, 0, Transform(Vec3(0.0f, 0.5f, 0.0f)));
    std::array<int32_t, kHumanBoneCount> partialMap{};
    partialMap.fill(-1);
    partialMap[humanBoneIndex(HumanBone::Hips)] = 0;
    partialMap[humanBoneIndex(HumanBone::Head)] = 1;
    HumanoidAvatar partialAvatar =
        createHumanoidAvatar(partial, partialMap);

    const int neckMuscle = findMuscleId("Neck Nod Down-Up");
    const int headMuscle = findMuscleId("Head Nod Down-Up");
    ASSERT_GE(neckMuscle, 0);
    ASSERT_GE(headMuscle, 0);

    HumanoidSample neckOnly;
    neckOnly.muscles[neckMuscle] = 1.0f;
    Pose neckPose;
    humanoidToLocal(neckOnly, partialAvatar, partial, neckPose);

    HumanoidSample headOnly;
    headOnly.muscles[headMuscle] = 1.0f;
    Pose headPose;
    humanoidToLocal(headOnly, partialAvatar, partial, headPose);

    EXPECT_GT(std::abs(glm::dot(neckPose.local[1].rotation,
                                headPose.local[1].rotation)),
              0.9999f);
}

TEST(Converter, RetargetFromPreservesReferenceGoalsAndRoot) {
    const std::string root = std::string(ANIM_TESTS_DIR) + "/golden/";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonFile(root + "DefaultHumanoid.skeleton.json", skeleton,
                                 error))
        << error;

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarFile(root + "DefaultHumanoid.source.avatar.json", config,
                               error))
        << error;

    HumanoidAvatar avatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, avatar, error)) << error;

    MuscleValues muscles;
    for (int i = 0; i < kMuscleCount; ++i) {
        muscles[i] = 0.35f * std::sin(static_cast<float>(i) * 0.29f);
    }

    HumanoidSample sourceSample;
    sourceSample.muscles = muscles;
    sourceSample.hasRootT = true;
    sourceSample.rootT = Vec3(0.3f, 0.9f, -0.2f);
    sourceSample.hasRootQ = true;
    sourceSample.rootQ = glm::angleAxis(radians(18.0f), Vec3(0.0f, 1.0f, 0.0f));
    sourceSample.hasFootT = sourceSample.hasFootQ = true;
    sourceSample.hasHandT = sourceSample.hasHandQ = true;
    sourceSample.leftFootT = Vec3(0.4f, 0.1f, 0.2f);
    sourceSample.rightFootT = Vec3(-0.4f, 0.1f, 0.2f);
    sourceSample.leftHandT = Vec3(0.5f, 1.2f, 0.3f);
    sourceSample.rightHandT = Vec3(-0.5f, 1.2f, 0.3f);

    Pose sourcePose;
    humanoidToLocal(sourceSample, avatar, skeleton.skeleton, sourcePose);
    const HumanoidSample expected =
        humanoidLocalToSample(avatar, skeleton.skeleton, sourcePose);

    const HumanoidSample actual =
        retargetHumanoidFromPose(avatar, skeleton.skeleton, sourcePose);
    const float scale = avatar.hipsHeight > 0.0f ? avatar.hipsHeight : 1.0f;

    EXPECT_NEAR(glm::distance(actual.rootT, expected.rootT / scale), 0.0f, 2e-4f);
    EXPECT_GT(std::abs(glm::dot(actual.rootQ, expected.rootQ)), 0.9999f);
    EXPECT_TRUE(actual.hasFootT);
    EXPECT_TRUE(actual.hasHandT);
    EXPECT_NEAR(glm::distance(actual.leftFootT, expected.leftFootT / scale), 0.0f,
                2e-4f);
    EXPECT_NEAR(glm::distance(actual.rightFootT, expected.rightFootT / scale), 0.0f,
                2e-4f);
    EXPECT_NEAR(glm::distance(actual.leftHandT, expected.leftHandT / scale), 0.0f,
                2e-4f);
    EXPECT_NEAR(glm::distance(actual.rightHandT, expected.rightHandT / scale), 0.0f,
                2e-4f);
    EXPECT_GT(std::abs(glm::dot(actual.leftFootQ, expected.leftFootQ)), 0.9999f);
    EXPECT_GT(std::abs(glm::dot(actual.rightFootQ, expected.rightFootQ)), 0.9999f);
    EXPECT_GT(std::abs(glm::dot(actual.leftHandQ, expected.leftHandQ)), 0.9999f);
    EXPECT_GT(std::abs(glm::dot(actual.rightHandQ, expected.rightHandQ)), 0.9999f);
}

TEST(Converter, CrossSkeletonLocalPoseRetarget) {
    const std::string root = std::string(ANIM_TESTS_DIR) + "/golden/";

    SkeletonAsset skeleton;
    std::string error;
    ASSERT_TRUE(loadSkeletonFile(root + "DefaultHumanoid.skeleton.json", skeleton,
                                 error))
        << error;

    AvatarConfig config;
    ASSERT_TRUE(loadAvatarFile(root + "DefaultHumanoid.source.avatar.json", config,
                               error))
        << error;

    HumanoidAvatar sourceAvatar;
    ASSERT_TRUE(buildHumanoidAvatar(skeleton, config, sourceAvatar, error)) << error;

    Skeleton other = skeleton.skeleton;
    for (std::size_t i = 0; i < other.bones.size(); ++i) {
        other.bones[i].restLocal.translation *= 1.0f + 0.05f * static_cast<float>(i % 5);
    }

    SkeletonAsset otherAsset;
    otherAsset.skeleton = other;
    otherAsset.boneNames = skeleton.boneNames;
    HumanoidAvatar otherAvatar;
    ASSERT_TRUE(buildHumanoidAvatar(otherAsset, config, otherAvatar, error)) << error;

    MuscleValues muscles;
    for (float& value : muscles.values) value = 0.4f;
    Pose sourcePose;
    muscleSpaceToLocal(muscles, sourceAvatar, skeleton.skeleton, sourcePose);
    Pose targetPose;
    retargetHumanoidLocalPose(sourceAvatar, skeleton.skeleton, sourcePose,
                              otherAvatar, other, targetPose);

    const HumanoidSample sourceSample =
        retargetHumanoidFromPose(sourceAvatar, skeleton.skeleton, sourcePose);
    Pose expected;
    humanoidToLocal(sourceSample, otherAvatar, other, expected,
                    otherAvatar.hipsHeight);
    for (int i = 0; i < kHumanBoneCount; ++i) {
        const int sourceBone = sourceAvatar.boneIndex(static_cast<HumanBone>(i));
        const int targetBone = otherAvatar.boneIndex(static_cast<HumanBone>(i));
        if (sourceBone < 0 || targetBone < 0) continue;
        EXPECT_GT(std::abs(glm::dot(
                      targetPose.local[static_cast<std::size_t>(targetBone)].rotation,
                      expected.local[static_cast<std::size_t>(targetBone)].rotation)),
                  0.9999f);
    }

    const int hips = otherAvatar.boneIndex(HumanBone::Hips);
    ASSERT_GE(hips, 0);
    const float scale = otherAvatar.hipsHeight / sourceAvatar.hipsHeight;
    const Vec3 sourceHips =
        sourcePose.local[static_cast<std::size_t>(sourceAvatar.boneIndex(HumanBone::Hips))]
            .translation;
    EXPECT_NEAR(glm::distance(
                    targetPose.local[static_cast<std::size_t>(hips)].translation,
                    sourceHips * scale),
                0.0f, 1e-4f);
}
