#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "SkeletonLoader.h"
#include "Validator.h"
#include "anim/clip/ClipIO.h"
#include "anim/core/Hash.h"
#include "anim/core/Math.h"
#include "anim/integration/Json.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;
using namespace anim::validate;

namespace {

Curve constantCurve(float value) {
    Curve curve;
    Keyframe key;
    key.time = 0.0f;
    key.value = value;
    key.inSlope = 0.0f;
    key.outSlope = 0.0f;
    key.inWeight = 1.0f / 3.0f;
    key.outWeight = 1.0f / 3.0f;
    curve.keys.push_back(key);
    return curve;
}

Curve timeCurve(float first, float second, float third) {
    Curve curve;
    for (const float time : {first, second, third}) {
        Keyframe key;
        key.time = time;
        key.value = 0.0f;
        key.inSlope = 0.0f;
        key.outSlope = 0.0f;
        key.inWeight = 1.0f / 3.0f;
        key.outWeight = 1.0f / 3.0f;
        curve.keys.push_back(key);
    }
    return curve;
}

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone(hashString("Root"), -1, Transform{});
    skeleton.addBone(hashString("Child"), 0, Transform{});
    return skeleton;
}

BoneLocalClip makeClip() {
    BoneLocalClip clip;
    clip.length = 1.0f;
    clip.averageSpeed = 1.0f;

    BoneChannel channel;
    channel.boneIndex = 0;
    channel.hasPosition = 1;
    channel.hasRotation = 1;
    channel.hasScale = 1;
    for (int i = 0; i < 3; ++i) {
        channel.position[i] = constantCurve(0.0f);
        channel.scale[i] = constantCurve(1.0f);
    }
    for (int i = 0; i < 4; ++i) {
        channel.rotation[i] = constantCurve(i == 3 ? 1.0f : 0.0f);
    }
    clip.channels.push_back(channel);
    return clip;
}

ClipValidationOptions optionsFor(const Skeleton* skeleton) {
    ClipValidationOptions options;
    options.file = "Walk.ab";
    options.clipName = "Walk";
    options.skeleton = skeleton;
    return options;
}

bool hasCode(const Report& report, const char* code) {
    for (const Issue& issue : report.issues) {
        if (issue.code == code) return true;
    }
    return false;
}

const Issue* findCode(const Report& report, const char* code) {
    for (const Issue& issue : report.issues) {
        if (issue.code == code) return &issue;
    }
    return nullptr;
}

std::vector<uint8_t> encode(const Clip& clip) {
    std::string error;
    std::vector<uint8_t> bytes = encodeClip(clip, &error);
    EXPECT_TRUE(error.empty()) << error;
    return bytes;
}

}  // namespace

TEST(Validation, AcceptsValidClipBytes) {
    const Skeleton skeleton = makeSkeleton();
    const BoneLocalClip clip = makeClip();
    const std::vector<uint8_t> bytes = encode(clip);
    ASSERT_FALSE(bytes.empty());

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    EXPECT_FALSE(report.hasErrors());
    EXPECT_EQ(report.count(Severity::Warning), 0u);
}

TEST(Validation, ReportsBadAbMagic) {
    const Skeleton skeleton = makeSkeleton();
    std::vector<uint8_t> bytes = encode(makeClip());
    ASSERT_FALSE(bytes.empty());
    bytes[0] = 0;

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "ab.magic"));
    EXPECT_TRUE(report.hasErrors());
}

TEST(Validation, ReportsBadAbVersion) {
    const Skeleton skeleton = makeSkeleton();
    std::vector<uint8_t> bytes = encode(makeClip());
    ASSERT_FALSE(bytes.empty());
    bytes[4] = 99;
    bytes[5] = 0;

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "ab.version"));
}

TEST(Validation, ReportsTruncatedAb) {
    const Skeleton skeleton = makeSkeleton();
    std::vector<uint8_t> bytes = encode(makeClip());
    ASSERT_GT(bytes.size(), 8u);
    bytes.resize(bytes.size() / 2);

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "ab.truncated"));
}

TEST(Validation, ReportsUnknownClipType) {
    const Skeleton skeleton = makeSkeleton();
    std::vector<uint8_t> bytes = encode(makeClip());
    ASSERT_GE(bytes.size(), 8u);
    bytes[6] = 99;
    bytes[7] = 0;

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "ab.type"));
}

TEST(Validation, ReportsBoneIndexOutOfRange) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    clip.channels[0].boneIndex = 3;
    const std::vector<uint8_t> bytes = encode(clip);
    ASSERT_FALSE(bytes.empty());

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    const Issue* issue = findCode(report, "clip.bone_index_range");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->severity, Severity::Error);
    EXPECT_EQ(issue->path, "channels[0].boneIndex");
}

TEST(Validation, ReportsNegativeBoneIndex) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    clip.channels[0].boneIndex = -1;

    Report report;
    validateClip(clip, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "clip.bone_index_negative"));
}

TEST(Validation, ReportsDuplicateBoneChannel) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    const BoneChannel duplicate = clip.channels[0];
    clip.channels.push_back(duplicate);
    const std::vector<uint8_t> bytes = encode(clip);
    ASSERT_FALSE(bytes.empty());

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "clip.channel_duplicate"));
}

TEST(Validation, ReportsMarkerTimeOutOfRange) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    clip.markers.push_back(EventMarker{1.5f, hashString("Footstep")});
    const std::vector<uint8_t> bytes = encode(clip);
    ASSERT_FALSE(bytes.empty());

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    const Issue* issue = findCode(report, "event.time_range");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->severity, Severity::Error);
}

TEST(Validation, ReportsInvalidClipLength) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    clip.length = std::numeric_limits<float>::quiet_NaN();
    const std::vector<uint8_t> bytes = encode(clip);
    ASSERT_FALSE(bytes.empty());

    Report report;
    validateClipBytes(bytes, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "clip.length_invalid"));
}

TEST(Validation, WarnsAboutZeroLengthClip) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    clip.length = 0.0f;

    Report report;
    validateClip(clip, optionsFor(&skeleton), report);
    const Issue* issue = findCode(report, "clip.length_zero");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->severity, Severity::Warning);
    EXPECT_FALSE(report.hasErrors());
}

TEST(Validation, ReportsCurveTimeOrder) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    clip.channels[0].position[0] = timeCurve(0.0f, 0.5f, 0.25f);

    Report report;
    validateClip(clip, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "curve.key_time_order"));
}

TEST(Validation, ReportsZeroQuaternion) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    for (int i = 0; i < 4; ++i) {
        clip.channels[0].rotation[i] = constantCurve(0.0f);
    }

    Report report;
    validateClip(clip, optionsFor(&skeleton), report);
    EXPECT_TRUE(hasCode(report, "curve.rotation_zero"));
}

TEST(Validation, WarnsAboutDuplicateEvent) {
    const Skeleton skeleton = makeSkeleton();
    BoneLocalClip clip = makeClip();
    clip.markers.push_back(EventMarker{0.5f, hashString("Footstep")});
    clip.markers.push_back(EventMarker{0.5f, hashString("Footstep")});

    Report report;
    validateClip(clip, optionsFor(&skeleton), report);
    const Issue* issue = findCode(report, "event.duplicate");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->severity, Severity::Warning);
}

TEST(Validation, ReportsEmptySkeleton) {
    const Skeleton skeleton;

    Report report;
    validateSkeleton(skeleton, "character.skeleton.json", nullptr, report);
    EXPECT_TRUE(hasCode(report, "skeleton.empty"));
}

TEST(Validation, ReportsInvalidSkeletonParentRange) {
    Skeleton skeleton;
    Bone root;
    root.name = hashString("Root");
    root.parent = -1;
    skeleton.bones.push_back(root);

    Bone bad;
    bad.name = hashString("Bad");
    bad.parent = 7;
    skeleton.bones.push_back(bad);

    Report report;
    validateSkeleton(skeleton, "character.skeleton.json", nullptr, report);
    EXPECT_TRUE(hasCode(report, "skeleton.parent_range"));
}

TEST(Validation, ReportsSkeletonParentOrder) {
    Skeleton skeleton;
    Bone bad;
    bad.name = hashString("Bad");
    bad.parent = 0;
    skeleton.bones.push_back(bad);

    Report report;
    validateSkeleton(skeleton, "character.skeleton.json", nullptr, report);
    EXPECT_TRUE(hasCode(report, "skeleton.parent_order"));
}

TEST(Validation, ReportsDuplicateSkeletonName) {
    Skeleton skeleton;
    Bone first;
    first.name = hashString("Root");
    first.parent = -1;
    skeleton.bones.push_back(first);

    Bone duplicate;
    duplicate.name = hashString("Root");
    duplicate.parent = -1;
    skeleton.bones.push_back(duplicate);

    Report report;
    validateSkeleton(skeleton, "character.skeleton.json", nullptr, report);
    EXPECT_TRUE(hasCode(report, "skeleton.name_duplicate"));
    EXPECT_TRUE(hasCode(report, "skeleton.root_multiple"));
}

TEST(Validation, ReportsNonFiniteSkeletonData) {
    Skeleton skeleton;
    Bone bone;
    bone.name = hashString("Root");
    bone.parent = -1;
    bone.restLocal.translation.x = std::numeric_limits<float>::quiet_NaN();
    bone.inverseBind[0][0] = std::numeric_limits<float>::infinity();
    skeleton.bones.push_back(bone);

    Report report;
    validateSkeleton(skeleton, "character.skeleton.json", nullptr, report);
    EXPECT_TRUE(hasCode(report, "skeleton.rest_non_finite"));
    EXPECT_TRUE(hasCode(report, "skeleton.inverse_bind_non_finite"));
}

TEST(Validation, AcceptsPackedSkeletonFixture) {
    const std::filesystem::path path =
        std::filesystem::path(ANIM_TESTS_DIR) / "fixtures" / "anim_pack" /
        "simple_rig.skeleton.json";
    anim::converter::SkeletonAsset asset;
    std::string error;
    ASSERT_TRUE(anim::converter::loadSkeletonFile(path.string(), asset, error)) << error;

    Report report;
    validateSkeleton(asset.skeleton, path.string(), &asset.boneNames, report);
    EXPECT_FALSE(report.hasErrors());
}

TEST(Validation, ReportSortsAndSerializesStableJson) {
    Report report;
    report.fileCount = 2;
    report.clipCount = 1;
    report.clipStats["Walk"] = ClipStats{1.0f, 2, 3, 1};
    report.add(Severity::Warning, "z.warning", "b.ab", "B", "p", "warning");
    report.add(Severity::Error, "a.error", "a.ab", "A", "p", "error");
    report.add(Severity::Info, "i.info", "c.ab", "C", "", "info");

    const std::string text = report.toJson(true);
    EXPECT_EQ(text, report.toJson(true));

    anim::json::Value root;
    std::string error;
    ASSERT_TRUE(anim::json::parse(text, root, error)) << error;
    ASSERT_TRUE(root.isObject());
    ASSERT_NE(root.find("ok"), nullptr);
    ASSERT_NE(root.find("strict"), nullptr);
    EXPECT_FALSE(root.find("ok")->boolean);
    EXPECT_TRUE(root.find("strict")->boolean);

    const anim::json::Value* summary = root.find("summary");
    ASSERT_NE(summary, nullptr);
    EXPECT_EQ(summary->find("files")->number, 2.0);
    EXPECT_EQ(summary->find("clips")->number, 1.0);
    EXPECT_EQ(summary->find("errors")->number, 1.0);
    EXPECT_EQ(summary->find("warnings")->number, 1.0);

    const anim::json::Value* issues = root.find("issues");
    ASSERT_NE(issues, nullptr);
    ASSERT_EQ(issues->array.size(), 3u);
    EXPECT_EQ(issues->array[0].find("severity")->text, "error");
    EXPECT_EQ(issues->array[0].find("code")->text, "a.error");
    EXPECT_EQ(issues->array[1].find("severity")->text, "warning");
    EXPECT_EQ(issues->array[2].find("severity")->text, "info");

    const anim::json::Value* stats = root.find("stats");
    ASSERT_NE(stats, nullptr);
    const anim::json::Value* clips = stats->find("clips");
    ASSERT_NE(clips, nullptr);
    const anim::json::Value* walk = clips->find("Walk");
    ASSERT_NE(walk, nullptr);
    EXPECT_EQ(walk->find("length")->number, 1.0);
    EXPECT_EQ(walk->find("channels")->number, 2.0);
    EXPECT_EQ(walk->find("markers")->number, 3.0);
    EXPECT_EQ(walk->find("events")->number, 1.0);
}

TEST(Validation, ManifestReportsRootBoneAndMissingEvent) {
    AnimationSet manifest;
    manifest.skeletonFile = "character.skeleton.json";
    ClipAssetInfo info;
    info.name = "Walk";
    info.action = "Walk";
    info.file = "Walk.ab";
    info.rootBone = "Missing";
    info.events.push_back(ClipEventInfo{0.5f, "Footstep"});
    manifest.clips.push_back(info);

    const Skeleton skeleton = makeSkeleton();
    const std::vector<std::string> names{"Root", "Child"};
    ManifestValidationOptions options;
    options.file = "anim_manifest.json";
    options.skeleton = &skeleton;
    options.boneNames = &names;

    Report report;
    validateManifest(manifest, options, report);
    EXPECT_TRUE(hasCode(report, "manifest.root_bone_missing"));

    const BoneLocalClip clip = makeClip();
    validateClipManifest(clip, info, 0, options, report);
    EXPECT_TRUE(hasCode(report, "manifest.event_missing"));
}

TEST(Validation, ManifestReportsDuplicateNamesAndActions) {
    AnimationSet manifest;
    manifest.skeletonFile = "character.skeleton.json";
    ClipAssetInfo first;
    first.name = "Walk";
    first.action = "Shared";
    first.file = "Walk.ab";
    ClipAssetInfo second = first;
    second.name = "Walk";
    second.action = "Shared";
    manifest.clips.push_back(first);
    manifest.clips.push_back(second);

    ManifestValidationOptions options;
    options.file = "anim_manifest.json";

    Report report;
    validateManifest(manifest, options, report);
    EXPECT_TRUE(hasCode(report, "manifest.clip_duplicate"));
    EXPECT_TRUE(hasCode(report, "manifest.action_duplicate"));
}

TEST(Validation, StrictWarningsChangeReportOk) {
    Report report;
    report.add(Severity::Warning, "warning.only", "file", {}, {}, "warning");

    anim::json::Value root;
    std::string error;
    ASSERT_TRUE(anim::json::parse(report.toJson(false), root, error)) << error;
    EXPECT_TRUE(root.find("ok")->boolean);

    anim::json::Value strictRoot;
    ASSERT_TRUE(anim::json::parse(report.toJson(true), strictRoot, error)) << error;
    EXPECT_FALSE(strictRoot.find("ok")->boolean);
}
