#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "SkeletonLoader.h"
#include "anim/integration/AnimationManifest.h"
#include "anim/integration/Json.h"
#include "anim/skeleton/Palette.h"

using namespace anim;

namespace {

void expectManifestError(const char* text, const char* expected) {
    AnimationSet set;
    std::string error;
    EXPECT_FALSE(loadAnimationManifest(text, set, error));
    EXPECT_NE(error.find(expected), std::string::npos) << error;
}

}  // namespace

TEST(AnimationManifest, LoadsFieldsAndResolvesPaths) {
    const std::string root = std::string(ANIM_TESTS_DIR) + "/fixtures/anim_pack/";
    const std::string path = root + "anim_manifest.json";

    AnimationSet set;
    std::string error;
    ASSERT_TRUE(loadAnimationManifestFile(path, set, error)) << error;

    EXPECT_EQ(set.version, 1);
    EXPECT_EQ(set.coordinate, "mirrored");
    EXPECT_EQ(std::filesystem::path(set.skeletonFile),
              std::filesystem::path(root) / "simple_rig.skeleton.json");
    ASSERT_EQ(set.clips.size(), 1u);

    const ClipAssetInfo& clip = set.clips[0];
    EXPECT_EQ(clip.name, "Walk");
    EXPECT_EQ(clip.action, "WalkAction");
    EXPECT_EQ(std::filesystem::path(clip.file),
              std::filesystem::path(root) / "Walk.ab");
    EXPECT_TRUE(clip.looping);
    EXPECT_FLOAT_EQ(clip.fadeDuration, 0.15f);
    EXPECT_FLOAT_EQ(clip.speed, 1.25f);
    EXPECT_FLOAT_EQ(clip.normalizedStartTime, 0.2f);
    EXPECT_EQ(clip.rootBone, "Hips");
    ASSERT_EQ(clip.events.size(), 2u);
    EXPECT_FLOAT_EQ(clip.events[0].normalizedTime, 0.1f);
    EXPECT_EQ(clip.events[0].name, "Footstep.Left");
    EXPECT_FLOAT_EQ(clip.events[1].normalizedTime, 0.6f);
    EXPECT_EQ(clip.events[1].name, "Footstep.Right");

    EXPECT_NE(set.find("Walk"), nullptr);
    EXPECT_EQ(set.find("Missing"), nullptr);
}

TEST(AnimationManifest, AppliesDefaultsWithoutMutatingPaths) {
    const char* text = R"({
        "version": 1,
        "skeleton": "simple_rig.skeleton.json",
        "clips": [
            {"name": "Idle", "file": "Idle.ab"}
        ]
    })";

    AnimationSet set;
    std::string error;
    ASSERT_TRUE(loadAnimationManifest(text, set, error)) << error;

    EXPECT_EQ(set.skeletonFile, "simple_rig.skeleton.json");
    EXPECT_EQ(set.coordinate, "gltf");
    ASSERT_EQ(set.clips.size(), 1u);
    EXPECT_EQ(set.clips[0].action, "Idle");
    EXPECT_FALSE(set.clips[0].looping);
    EXPECT_FLOAT_EQ(set.clips[0].fadeDuration, 0.25f);
    EXPECT_FLOAT_EQ(set.clips[0].speed, 1.0f);
    EXPECT_TRUE(std::isnan(set.clips[0].normalizedStartTime));
    EXPECT_TRUE(set.clips[0].rootBone.empty());
    EXPECT_TRUE(set.clips[0].events.empty());
}

TEST(AnimationManifest, RejectsInvalidFields) {
    expectManifestError(R"({"skeleton": "s.skeleton.json", "clips": []})",
                        "version");
    expectManifestError(
        R"({"version": 2, "skeleton": "s.skeleton.json", "clips": []})",
        "version");
    expectManifestError(
        R"({"version": 1, "skeleton": "", "clips": []})", "skeleton");
    expectManifestError(
        R"({"version": 1, "skeleton": "s.skeleton.json", "coordinate": "unity",
            "clips": []})",
        "coordinate");
    expectManifestError(
        R"({"version": 1, "skeleton": "s.skeleton.json",
            "clips": [{"name": "A", "file": "A.ab"},
                      {"name": "A", "file": "B.ab"}]})",
        "duplicates clip name");
    expectManifestError(
        R"({"version": 1, "skeleton": "s.skeleton.json",
            "clips": [{"name": "A", "file": "A.ab", "speed": 0}]})",
        "speed");
    expectManifestError(
        R"({"version": 1, "skeleton": "s.skeleton.json",
            "clips": [{"name": "A", "file": "A.ab",
                       "events": [{"normalizedTime": 1.5, "name": "Hit"}]}]})",
        "normalizedTime");
    expectManifestError(
        R"({"version": 1, "skeleton": "s.skeleton.json",
            "clips": [{"name": "A", "file": "A.ab",
                       "events": [{"normalizedTime": 0.5, "name": ""}]}]})",
        "name");
}

TEST(SkeletonLoader, UsesExplicitAndComputedInverseBind) {
    const char* text = R"({
        "bones": [
            {"name": "Root", "parent": -1, "translation": [1, 0, 0],
             "inverseBind": [1, 0, 0, 0,
                             0, 1, 0, 0,
                             0, 0, 1, 0,
                             5, 6, 7, 1]},
            {"name": "Child", "parent": 0, "translation": [0, 2, 0]}
        ]
    })";

    converter::SkeletonAsset asset;
    std::string error;
    ASSERT_TRUE(converter::loadSkeletonJson(text, asset, error)) << error;
    ASSERT_EQ(asset.skeleton.boneCount(), 2);

    EXPECT_FLOAT_EQ(asset.skeleton.bones[0].inverseBind[3][0], 5.0f);
    EXPECT_FLOAT_EQ(asset.skeleton.bones[0].inverseBind[3][1], 6.0f);
    EXPECT_FLOAT_EQ(asset.skeleton.bones[0].inverseBind[3][2], 7.0f);

    Pose rest;
    rest.resizeLike(asset.skeleton);
    rest.resetToRest(asset.skeleton);
    std::vector<Mat4> world;
    computeWorldMatrices(asset.skeleton, rest, world);
    EXPECT_EQ(asset.skeleton.bones[1].inverseBind, glm::inverse(world[1]));
}

TEST(AnimPackFixture, HasTwoActionsAndAllInterpolationModes) {
    const std::string path =
        std::string(ANIM_TESTS_DIR) + "/fixtures/anim_pack/simple_rig.gltf";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file) << path;
    std::ostringstream stream;
    stream << file.rdbuf();

    json::Value root;
    std::string error;
    ASSERT_TRUE(json::parse(stream.str(), root, error)) << error;

    const json::Value* animations = root.find("animations");
    ASSERT_NE(animations, nullptr);
    ASSERT_EQ(animations->array.size(), 2u);
    EXPECT_EQ(animations->array[0].find("name")->text, "Walk");
    EXPECT_EQ(animations->array[1].find("name")->text, "Wave");

    bool linear = false;
    bool step = false;
    bool cubic = false;
    for (const json::Value& animation : animations->array) {
        const json::Value* samplers = animation.find("samplers");
        ASSERT_NE(samplers, nullptr);
        for (const json::Value& sampler : samplers->array) {
            const std::string& interpolation = sampler.find("interpolation")->text;
            linear = linear || interpolation == "LINEAR";
            step = step || interpolation == "STEP";
            cubic = cubic || interpolation == "CUBICSPLINE";
        }
    }
    EXPECT_TRUE(linear);
    EXPECT_TRUE(step);
    EXPECT_TRUE(cubic);

    const json::Value* skins = root.find("skins");
    ASSERT_NE(skins, nullptr);
    ASSERT_EQ(skins->array.size(), 1u);
    EXPECT_EQ(skins->array[0].find("joints")->array.size(), 3u);
}

TEST(SkeletonLoader, RejectsInvalidHierarchyAndInverseBind) {
    converter::SkeletonAsset asset;
    std::string error;
    EXPECT_FALSE(converter::loadSkeletonJson(
        R"({"bones": [{"name": "Child", "parent": 1}]})", asset, error));
    EXPECT_NE(error.find("parent"), std::string::npos) << error;

    error.clear();
    EXPECT_FALSE(converter::loadSkeletonJson(
        R"({"bones": [{"name": "Root", "parent": -1,
                       "inverseBind": [1, 0, 0]}]})",
        asset, error));
    EXPECT_NE(error.find("inverseBind"), std::string::npos) << error;

    error.clear();
    EXPECT_FALSE(converter::loadSkeletonJson(
        R"({"version": 2, "bones": [{"name": "Root", "parent": -1}]})",
        asset, error));
    EXPECT_NE(error.find("version"), std::string::npos) << error;
}
