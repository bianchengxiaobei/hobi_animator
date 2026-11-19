#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "AnimPack.h"
#include "ClipBuilder.h"
#include "EventSidecar.h"
#include "GltfImporter.h"
#include "SkeletonLoader.h"
#include "anim/hobi/HobiAnimator.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/Transition.h"
#include "anim/clip/ClipIO.h"
#include "anim/integration/AnimationManifest.h"
#include "anim/integration/Coordinate.h"
#include "anim/skeleton/Pose.h"

using namespace anim;

namespace {

namespace fs = std::filesystem;

class TempDir {
public:
    TempDir() {
        static int counter = 0;
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        _path = fs::temp_directory_path() /
                ("anim_pack_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        std::error_code ec;
        fs::remove_all(_path, ec);
        fs::create_directories(_path, ec);
        EXPECT_FALSE(ec) << ec.message();
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(_path, ec);
    }

    const fs::path& path() const { return _path; }

private:
    fs::path _path;
};

fs::path fixtureRoot() {
    return fs::path(ANIM_TESTS_DIR) / "fixtures" / "anim_pack";
}

pack::PackOptions makeOptions(const TempDir& temp, const std::string& coordinate = "") {
    pack::PackOptions options;
    options.input = (fixtureRoot() / "simple_rig.gltf").string();
    options.manifest = (fixtureRoot() / "simple_rig_manifest.json").string();
    options.outDir = temp.path().string();
    options.coordinate = coordinate;
    return options;
}

std::vector<uint8_t> readBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

std::unique_ptr<BoneLocalClip> loadClip(const fs::path& path) {
    const std::vector<uint8_t> bytes = readBytes(path);
    if (bytes.empty()) return nullptr;
    auto clip = decodeClip(bytes);
    auto* local = dynamic_cast<BoneLocalClip*>(clip.get());
    if (local == nullptr) return nullptr;
    clip.release();
    return std::unique_ptr<BoneLocalClip>(local);
}

}  // namespace

TEST(AnimPack, PacksSkeletonClipsAndInterpolation) {
    TempDir temp;
    const pack::PackOptions options = makeOptions(temp);

    pack::PackResult result;
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(pack::packAnimations(options, result, warnings, error)) << error;
    EXPECT_EQ(result.boneCount, 3);
    EXPECT_EQ(result.animationCount, 2);
    ASSERT_EQ(result.clips.size(), 2u);
    EXPECT_EQ(result.clips[0].name, "Walk");
    EXPECT_EQ(result.clips[0].file, "Walk.ab");
    EXPECT_EQ(result.clips[0].eventCount, 2);
    EXPECT_EQ(result.clips[1].name, "Wave");

    const fs::path skeletonPath = temp.path() / "simple_rig.skeleton.json";
    ASSERT_TRUE(fs::exists(skeletonPath));
    converter::SkeletonAsset skeleton;
    ASSERT_TRUE(converter::loadSkeletonFile(skeletonPath.string(), skeleton, error)) << error;
    ASSERT_EQ(skeleton.skeleton.boneCount(), 3);
    EXPECT_EQ(skeleton.boneNames[0], "Root");
    EXPECT_EQ(skeleton.boneNames[1], "Hips");
    EXPECT_EQ(skeleton.boneNames[2], "Spine");
    EXPECT_EQ(skeleton.skeleton.bones[0].parent, -1);
    EXPECT_EQ(skeleton.skeleton.bones[1].parent, 0);
    EXPECT_EQ(skeleton.skeleton.bones[2].parent, 1);
    EXPECT_FLOAT_EQ(skeleton.skeleton.bones[1].restLocal.translation.y, 1.0f);
    EXPECT_FLOAT_EQ(skeleton.skeleton.bones[2].restLocal.translation.y, 0.5f);
    EXPECT_FLOAT_EQ(skeleton.skeleton.bones[1].inverseBind[3][1], -1.0f);
    EXPECT_FLOAT_EQ(skeleton.skeleton.bones[2].inverseBind[3][1], -1.5f);

    const fs::path manifestPath = temp.path() / "simple_rig_manifest.json";
    AnimationSet manifest;
    ASSERT_TRUE(loadAnimationManifestFile(manifestPath.string(), manifest, error)) << error;
    EXPECT_EQ(manifest.coordinate, "gltf");
    EXPECT_EQ(fs::path(manifest.skeletonFile), skeletonPath);
    ASSERT_EQ(manifest.clips.size(), 2u);
    EXPECT_EQ(manifest.clips[0].name, "Walk");
    EXPECT_EQ(manifest.clips[0].action, "Walk");
    EXPECT_EQ(fs::path(manifest.clips[0].file), temp.path() / "Walk.ab");
    EXPECT_TRUE(manifest.clips[0].looping);
    EXPECT_FLOAT_EQ(manifest.clips[0].fadeDuration, 0.25f);
    EXPECT_EQ(manifest.clips[0].rootBone, "Hips");
    ASSERT_EQ(manifest.clips[0].events.size(), 2u);
    EXPECT_FLOAT_EQ(manifest.clips[0].events[0].normalizedTime, 0.25f);
    EXPECT_EQ(manifest.clips[0].events[0].name, "Footstep.Left");

    const auto walk = loadClip(temp.path() / "Walk.ab");
    ASSERT_NE(walk, nullptr);
    EXPECT_FLOAT_EQ(walk->length, 1.0f);
    EXPECT_TRUE(walk->looping);
    ASSERT_EQ(walk->channels.size(), 2u);
    ASSERT_EQ(walk->markers.size(), 2u);

    Pose pose;
    pose.resizeLike(skeleton.skeleton);
    sampleClip(*walk, 0.5f, skeleton.skeleton, nullptr, pose);
    EXPECT_NEAR(pose[0].translation.x, 0.0f, 1e-6f);
    EXPECT_NEAR(pose[0].translation.y, 0.05f, 1e-6f);
    EXPECT_NEAR(pose[0].translation.z, 0.1f, 1e-6f);
    EXPECT_NEAR(std::abs(glm::dot(pose[1].rotation, kIdentityQuat)), 1.0f, 1e-6f);

    sampleClip(*walk, 1.0f, skeleton.skeleton, nullptr, pose);
    EXPECT_NEAR(std::abs(glm::dot(
                    pose[1].rotation, glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f)))),
                1.0f, 1e-5f);

    const auto wave = loadClip(temp.path() / "Wave.ab");
    ASSERT_NE(wave, nullptr);
    EXPECT_FLOAT_EQ(wave->length, 1.0f);
    EXPECT_FALSE(wave->looping);
    ASSERT_EQ(wave->channels.size(), 1u);
    sampleClip(*wave, 0.5f, skeleton.skeleton, nullptr, pose);
    EXPECT_NEAR(pose[2].translation.x, 0.0f, 1e-6f);
    EXPECT_NEAR(pose[2].translation.y, 0.1f, 1e-6f);
    EXPECT_NEAR(pose[2].translation.z, 0.15f, 1e-6f);
}

TEST(AnimPack, PacksGlbInput) {
    TempDir temp;
    pack::PackOptions options = makeOptions(temp);
    options.input = (fixtureRoot() / "simple_rig.glb").string();

    pack::PackResult result;
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(pack::packAnimations(options, result, warnings, error)) << error;
    EXPECT_EQ(result.boneCount, 3);
    ASSERT_EQ(result.clips.size(), 2u);

    const auto walk = loadClip(temp.path() / "Walk.ab");
    ASSERT_NE(walk, nullptr);
    EXPECT_FLOAT_EQ(walk->length, 1.0f);
    EXPECT_TRUE(fs::exists(temp.path() / "simple_rig.skeleton.json"));
}

TEST(AnimPack, AppliesEventSidecar) {
    TempDir temp;
    pack::PackOptions options = makeOptions(temp);
    options.events = (fixtureRoot() / "simple_rig_events.json").string();
    pack::PackResult result;
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(pack::packAnimations(options, result, warnings, error)) << error;
    ASSERT_EQ(result.clips.size(), 2u);
    EXPECT_EQ(result.clips[0].eventCount, 2);
    EXPECT_EQ(result.clips[1].eventCount, 1);

    AnimationSet manifest;
    ASSERT_TRUE(loadAnimationManifestFile(
        (temp.path() / "simple_rig_manifest.json").string(), manifest, error))
        << error;
    ASSERT_EQ(manifest.clips.size(), 2u);
    ASSERT_EQ(manifest.clips[0].events.size(), 2u);
    EXPECT_FLOAT_EQ(manifest.clips[0].events[0].normalizedTime, 0.0f);
    EXPECT_EQ(manifest.clips[0].events[0].name, "Footstep.Left");
    EXPECT_FLOAT_EQ(manifest.clips[0].events[1].normalizedTime, 0.5f);
    EXPECT_EQ(manifest.clips[0].events[1].name, "Footstep.Right");
    ASSERT_EQ(manifest.clips[1].events.size(), 1u);
    EXPECT_FLOAT_EQ(manifest.clips[1].events[0].normalizedTime, 0.25f);
    EXPECT_EQ(manifest.clips[1].events[0].name, "Wave.Start");

    const auto walk = loadClip(temp.path() / "Walk.ab");
    ASSERT_NE(walk, nullptr);
    ASSERT_EQ(walk->markers.size(), 2u);
    EXPECT_FLOAT_EQ(walk->markers[0].normalizedTime, 0.0f);
    EXPECT_FLOAT_EQ(walk->markers[1].normalizedTime, 0.5f);
}

TEST(AnimPack, MatchesPrefixedEventSidecarAction) {
    TempDir temp;
    const fs::path eventsPath = temp.path() / "events.json";
    {
        std::ofstream file(eventsPath, std::ios::binary);
        file << R"({"version":1,"fps":24,"actions":{
            "Armature|Walk":{"frameStart":0,"frameEnd":24,
                "events":[{"frame":12,"name":"Footstep.Right"}]},
            "Armature|Wave":{"frameStart":0,"frameEnd":24,
                "events":[{"frame":6,"name":"Wave.Start"}]}}})";
    }

    pack::PackOptions options = makeOptions(temp);
    options.events = eventsPath.string();
    options.stripActionPrefix = "Armature|";
    pack::PackResult result;
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(pack::packAnimations(options, result, warnings, error)) << error;

    AnimationSet manifest;
    ASSERT_TRUE(loadAnimationManifestFile(
        (temp.path() / "simple_rig_manifest.json").string(), manifest, error))
        << error;
    ASSERT_EQ(manifest.clips[0].events.size(), 1u);
    EXPECT_FLOAT_EQ(manifest.clips[0].events[0].normalizedTime, 0.5f);
    EXPECT_EQ(manifest.clips[0].events[0].name, "Footstep.Right");
    ASSERT_EQ(manifest.clips[1].events.size(), 1u);
    EXPECT_EQ(manifest.clips[1].events[0].name, "Wave.Start");
}

TEST(AnimPack, RejectsInvalidEventSidecar) {
    pack::EventSidecar sidecar;
    std::string error;
    EXPECT_FALSE(pack::loadEventSidecar(
        R"({"version":2,"fps":24,"actions":{}})", sidecar, error));
    EXPECT_NE(error.find("version"), std::string::npos) << error;

    error.clear();
    EXPECT_FALSE(pack::loadEventSidecar(
        R"({"version":1,"fps":0,"actions":{}})", sidecar, error));
    EXPECT_NE(error.find("fps"), std::string::npos) << error;

    error.clear();
    EXPECT_FALSE(pack::loadEventSidecar(
        R"({"version":1,"fps":24,"actions":{"Walk":{"frameStart":0,
            "frameEnd":24,"events":[{"frame":25,"name":"Late"}]}}})",
        sidecar, error));
    EXPECT_NE(error.find("within"), std::string::npos) << error;

    error.clear();
    EXPECT_FALSE(pack::loadEventSidecar(
        R"({"version":1,"fps":24,"actions":{"Walk":{"frameStart":24,
            "frameEnd":24,"events":[]}}})",
        sidecar, error));
    EXPECT_NE(error.find("greater"), std::string::npos) << error;

    TempDir temp;
    const fs::path eventsPath = temp.path() / "events.json";
    {
        std::ofstream file(eventsPath, std::ios::binary);
        file << R"({"version":1,"fps":24,"actions":{"Missing":{
            "frameStart":0,"frameEnd":24,"events":[]}}})";
    }
    pack::PackOptions options = makeOptions(temp);
    options.events = eventsPath.string();
    pack::PackResult result;
    std::vector<std::string> warnings;
    EXPECT_FALSE(pack::packAnimations(options, result, warnings, error));
    EXPECT_NE(error.find("not used"), std::string::npos) << error;
}

TEST(AnimPack, RuntimePlaysPackedClip) {
    TempDir temp;
    pack::PackOptions options = makeOptions(temp);
    pack::PackResult result;
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(pack::packAnimations(options, result, warnings, error)) << error;

    converter::SkeletonAsset skeleton;
    ASSERT_TRUE(converter::loadSkeletonFile(
        (temp.path() / "simple_rig.skeleton.json").string(), skeleton, error))
        << error;
    const auto walk = loadClip(temp.path() / "Walk.ab");
    ASSERT_NE(walk, nullptr);

    HobiAnimator animator;
    animator.initialize(skeleton.skeleton);
    ClipTransition transition;
    transition.clip = walk.get();
    transition.fadeDurationValue = 0.0f;
    HobiState* state = animator.play(transition);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->isPlaying());

    animator.update(0.25f);
    EXPECT_NEAR(state->normalizedTime(), 0.25f, 1e-5f);
    const Pose& pose = animator.playable().outputPose();
    EXPECT_NEAR(pose.local[0].translation.x, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.local[0].translation.y, 0.025f, 1e-6f);
    EXPECT_NEAR(pose.local[0].translation.z, 0.05f, 1e-6f);
    EXPECT_EQ(animator.palette().size(), 3u);
}

TEST(AnimPack, MergesNonJointParentChain) {
    const fs::path path = fixtureRoot() / "non_joint_parent.gltf";
    pack::ImportedAsset asset;
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(pack::importGltfFile(path.string(), asset, warnings, error)) << error;
    ASSERT_EQ(asset.bones.size(), 2u);
    EXPECT_EQ(asset.bones[0].name, "Root");
    EXPECT_EQ(asset.bones[0].parent, -1);
    EXPECT_NEAR(asset.bones[0].restLocal.translation.x, 1.0f, 1e-6f);
    EXPECT_NEAR(asset.bones[0].restLocal.translation.y, 10.0f, 1e-6f);
    EXPECT_NEAR(asset.bones[0].inverseBind[3][0], -1.0f, 1e-6f);
    EXPECT_NEAR(asset.bones[0].inverseBind[3][1], -10.0f, 1e-6f);

    EXPECT_EQ(asset.bones[1].name, "Hips");
    EXPECT_EQ(asset.bones[1].parent, 0);
    EXPECT_NEAR(asset.bones[1].restLocal.translation.y, 2.0f, 1e-6f);
    EXPECT_NEAR(asset.bones[1].inverseBind[3][0], -1.0f, 1e-6f);
    EXPECT_NEAR(asset.bones[1].inverseBind[3][1], -12.0f, 1e-6f);

    ASSERT_EQ(asset.animations.size(), 1u);
    ASSERT_EQ(asset.animations[0].channels.size(), 1u);
    const pack::ImportedChannel& channel = asset.animations[0].channels[0];
    ASSERT_EQ(channel.keys.size(), 2u);
    EXPECT_NEAR(channel.keys[0].value[0], 1.0f, 1e-6f);
    EXPECT_NEAR(channel.keys[0].value[1], 10.0f, 1e-6f);
    EXPECT_NEAR(channel.keys[0].value[2], 0.0f, 1e-6f);
    EXPECT_NEAR(channel.keys[1].value[0], 1.0f, 1e-6f);
    EXPECT_NEAR(channel.keys[1].value[1], 10.0f, 1e-6f);
    EXPECT_NEAR(channel.keys[1].value[2], 1.0f, 1e-6f);
}

TEST(AnimPack, MirrorsSkeletonAndAnimationCoordinates) {
    TempDir normalTemp;
    TempDir mirroredTemp;
    const pack::PackOptions normalOptions = makeOptions(normalTemp);
    const pack::PackOptions mirroredOptions = makeOptions(mirroredTemp, "mirrored");

    pack::PackResult normalResult;
    pack::PackResult mirroredResult;
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(pack::packAnimations(normalOptions, normalResult, warnings, error)) << error;
    ASSERT_TRUE(pack::packAnimations(mirroredOptions, mirroredResult, warnings, error))
        << error;

    converter::SkeletonAsset normal;
    converter::SkeletonAsset mirrored;
    ASSERT_TRUE(converter::loadSkeletonFile(
        (normalTemp.path() / "simple_rig.skeleton.json").string(), normal, error))
        << error;
    ASSERT_TRUE(converter::loadSkeletonFile(
        (mirroredTemp.path() / "simple_rig.skeleton.json").string(), mirrored, error))
        << error;
    ASSERT_EQ(normal.skeleton.boneCount(), mirrored.skeleton.boneCount());

    for (int i = 0; i < normal.skeleton.boneCount(); ++i) {
        const Transform expected =
            flipZ(normal.skeleton.bones[static_cast<std::size_t>(i)].restLocal);
        const Transform& actual =
            mirrored.skeleton.bones[static_cast<std::size_t>(i)].restLocal;
        EXPECT_NEAR(actual.translation.x, expected.translation.x, 1e-6f);
        EXPECT_NEAR(actual.translation.y, expected.translation.y, 1e-6f);
        EXPECT_NEAR(actual.translation.z, expected.translation.z, 1e-6f);
        EXPECT_NEAR(std::abs(glm::dot(actual.rotation, expected.rotation)), 1.0f, 1e-6f);
        EXPECT_NEAR(actual.scale.x, expected.scale.x, 1e-6f);
        EXPECT_NEAR(actual.scale.y, expected.scale.y, 1e-6f);
        EXPECT_NEAR(actual.scale.z, expected.scale.z, 1e-6f);

        const Mat4 expectedBind =
            flipZ(normal.skeleton.bones[static_cast<std::size_t>(i)].inverseBind);
        const Mat4& actualBind =
            mirrored.skeleton.bones[static_cast<std::size_t>(i)].inverseBind;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                EXPECT_NEAR(actualBind[column][row], expectedBind[column][row], 1e-6f);
            }
        }
    }

    const auto normalWalk = loadClip(normalTemp.path() / "Walk.ab");
    const auto mirroredWalk = loadClip(mirroredTemp.path() / "Walk.ab");
    ASSERT_NE(normalWalk, nullptr);
    ASSERT_NE(mirroredWalk, nullptr);

    Pose normalPose;
    Pose mirroredPose;
    normalPose.resizeLike(normal.skeleton);
    mirroredPose.resizeLike(mirrored.skeleton);
    sampleClip(*normalWalk, 0.5f, normal.skeleton, nullptr, normalPose);
    sampleClip(*mirroredWalk, 0.5f, mirrored.skeleton, nullptr, mirroredPose);
    EXPECT_NEAR(mirroredPose[0].translation.z, -normalPose[0].translation.z, 1e-6f);

    sampleClip(*normalWalk, 1.0f, normal.skeleton, nullptr, normalPose);
    sampleClip(*mirroredWalk, 1.0f, mirrored.skeleton, nullptr, mirroredPose);
    const Quat expectedRotation = flipZ(normalPose[1].rotation);
    EXPECT_NEAR(std::abs(glm::dot(mirroredPose[1].rotation, expectedRotation)), 1.0f,
                1e-5f);

    AnimationSet manifest;
    ASSERT_TRUE(loadAnimationManifestFile(
        (mirroredTemp.path() / "simple_rig_manifest.json").string(), manifest, error))
        << error;
    EXPECT_EQ(manifest.coordinate, "mirrored");
}

TEST(AnimPack, RejectsInvalidInputs) {
    TempDir temp;
    pack::PackResult result;
    std::vector<std::string> warnings;
    std::string error;

    pack::PackOptions missingInput = makeOptions(temp);
    missingInput.input.clear();
    EXPECT_FALSE(pack::packAnimations(missingInput, result, warnings, error));
    EXPECT_NE(error.find("--input"), std::string::npos) << error;

    pack::PackOptions wrongAction = makeOptions(temp);
    wrongAction.manifest = (fixtureRoot() / "anim_manifest.json").string();
    EXPECT_FALSE(pack::packAnimations(wrongAction, result, warnings, error));
    EXPECT_NE(error.find("WalkAction"), std::string::npos) << error;

    pack::PackOptions badCoordinate = makeOptions(temp, "unity");
    EXPECT_FALSE(pack::packAnimations(badCoordinate, result, warnings, error));
    EXPECT_NE(error.find("coordinate"), std::string::npos) << error;

    const fs::path noSkinPath = temp.path() / "no_skin.gltf";
    {
        std::ofstream file(noSkinPath, std::ios::binary);
        file << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[]}],"nodes":[]})";
    }
    pack::PackOptions noSkin = makeOptions(temp);
    noSkin.input = noSkinPath.string();
    EXPECT_FALSE(pack::packAnimations(noSkin, result, warnings, error));
    EXPECT_NE(error.find("no skin"), std::string::npos) << error;

    const fs::path duplicatePath = temp.path() / "duplicate.json";
    {
        std::ofstream file(duplicatePath, std::ios::binary);
        file << R"({"version":1,"skeleton":"s.json","clips":[
            {"name":"A","action":"Walk","file":"Same.ab"},
            {"name":"B","action":"Wave","file":"Same.ab"}]})";
    }
    pack::PackOptions duplicate = makeOptions(temp);
    duplicate.manifest = duplicatePath.string();
    EXPECT_FALSE(pack::packAnimations(duplicate, result, warnings, error));
    EXPECT_NE(error.find("already in use"), std::string::npos) << error;
}

TEST(AnimPack, MirrorsCubicSplineQuaternionTangents) {
    pack::ImportedAsset asset;
    pack::ImportedBone bone;
    bone.name = "Hips";
    asset.bones.push_back(bone);

    pack::ImportedAnimation animation;
    animation.name = "Cubic";
    animation.length = 1.0f;
    pack::ImportedChannel channel;
    channel.boneIndex = 0;
    channel.path = pack::AnimationPath::Rotation;
    channel.interpolation = pack::Interpolation::CubicSpline;
    channel.keys.resize(2);
    channel.keys[0].time = 0.0f;
    channel.keys[0].value[3] = 1.0f;
    channel.keys[0].inTangent[0] = 0.1f;
    channel.keys[0].inTangent[1] = 0.2f;
    channel.keys[0].inTangent[2] = 0.3f;
    channel.keys[0].inTangent[3] = 0.4f;
    channel.keys[0].outTangent[0] = 0.5f;
    channel.keys[0].outTangent[1] = 0.6f;
    channel.keys[0].outTangent[2] = 0.7f;
    channel.keys[0].outTangent[3] = 0.8f;
    channel.keys[1].time = 1.0f;
    channel.keys[1].value[1] = 0.7071068f;
    channel.keys[1].value[3] = 0.7071068f;
    channel.keys[1].inTangent[0] = 0.9f;
    channel.keys[1].inTangent[1] = 1.0f;
    channel.keys[1].inTangent[2] = 1.1f;
    channel.keys[1].inTangent[3] = 1.2f;
    channel.keys[1].outTangent[0] = 1.3f;
    channel.keys[1].outTangent[1] = 1.4f;
    channel.keys[1].outTangent[2] = 1.5f;
    channel.keys[1].outTangent[3] = 1.6f;
    animation.channels.push_back(channel);

    ClipAssetInfo info;
    info.name = "Cubic";
    info.file = "Cubic.ab";

    BoneLocalClip normal;
    BoneLocalClip mirrored;
    std::string error;
    ASSERT_TRUE(pack::buildBoneLocalClip(animation, asset, info, false, normal, error))
        << error;
    ASSERT_TRUE(pack::buildBoneLocalClip(animation, asset, info, true, mirrored, error))
        << error;
    ASSERT_EQ(normal.channels.size(), 1u);
    ASSERT_EQ(mirrored.channels.size(), 1u);

    const BoneChannel& normalChannel = normal.channels[0];
    const BoneChannel& mirroredChannel = mirrored.channels[0];
    ASSERT_EQ(normalChannel.rotation[0].keys.size(), 2u);
    ASSERT_EQ(mirroredChannel.rotation[0].keys.size(), 2u);
    EXPECT_NEAR(normalChannel.rotation[0].keys[0].inSlope, 0.1f, 1e-6f);

    EXPECT_NEAR(mirroredChannel.rotation[0].keys[0].inSlope, -0.1f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[1].keys[0].inSlope, -0.2f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[2].keys[0].inSlope, 0.3f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[3].keys[0].inSlope, 0.4f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[0].keys[1].outSlope, -1.3f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[1].keys[1].outSlope, -1.4f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[2].keys[1].outSlope, 1.5f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[3].keys[1].outSlope, 1.6f, 1e-6f);

    EXPECT_NEAR(mirroredChannel.rotation[0].keys[1].value, 0.0f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[1].keys[1].value, -0.7071068f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[2].keys[1].value, 0.0f, 1e-6f);
    EXPECT_NEAR(mirroredChannel.rotation[3].keys[1].value, 0.7071068f, 1e-6f);
}

TEST(AnimPack, ProducesStableBytes) {
    TempDir first;
    TempDir second;
    pack::PackResult result;
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(pack::packAnimations(makeOptions(first), result, warnings, error)) << error;
    ASSERT_TRUE(pack::packAnimations(makeOptions(second), result, warnings, error)) << error;

    for (const char* name : {"simple_rig.skeleton.json", "simple_rig_manifest.json",
                             "Walk.ab", "Wave.ab"}) {
        const std::vector<uint8_t> a = readBytes(first.path() / name);
        const std::vector<uint8_t> b = readBytes(second.path() / name);
        ASSERT_EQ(a.size(), b.size()) << name;
        EXPECT_EQ(a, b) << name;
    }
}
