#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "anim/hobi/HobiAnimator.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/clip/ClipIO.h"
#include "anim/core/Transform.h"
#include "anim/integration/AnimationClipLibrary.h"
#include "anim/integration/AnimationTransitions.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

namespace fs = std::filesystem;

class TempDir {
public:
    TempDir() {
        static int counter = 0;
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        _path = fs::temp_directory_path() /
                ("anim_runtime_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
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

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    skeleton.addBone("Hips"_sid, 0, Transform::identity());
    return skeleton;
}

BoneLocalClip makeClip(float length = 1.0f) {
    BoneLocalClip clip;
    clip.length = length;
    clip.looping = false;

    BoneChannel channel;
    channel.boneIndex = 1;
    channel.hasPosition = 1;
    channel.position[0].keys = {
        Keyframe{0.0f, 0.0f, 10.0f, 10.0f},
        Keyframe{length, 10.0f, 10.0f, 10.0f},
    };

    clip.channels.push_back(channel);
    return clip;
}

std::string writeClip(const TempDir& temp, const std::string& name) {
    const std::vector<uint8_t> bytes = encodeClip(makeClip());
    EXPECT_FALSE(bytes.empty());

    const fs::path path = temp.path() / (name + ".ab");
    std::ofstream file(path, std::ios::binary);
    EXPECT_TRUE(file);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

ClipAssetInfo makeInfo(const std::string& name, const std::string& file) {
    ClipAssetInfo info;
    info.name = name;
    info.action = name;
    info.file = file;
    info.looping = true;
    info.fadeDuration = 0.15f;
    info.speed = 1.25f;
    info.normalizedStartTime = 0.2f;
    info.rootBone = "Hips";
    info.events = {
        ClipEventInfo{0.25f, "Footstep.Left"},
        ClipEventInfo{0.75f, "Footstep.Right"},
    };
    return info;
}

}

TEST(AnimationClipLibrary, LoadsClipsAndAppliesManifestLoop) {
    TempDir temp;
    const std::string file = writeClip(temp, "Walk");

    AnimationSet set;
    set.clips.push_back(makeInfo("Walk", file));

    AnimationClipLibrary library;
    std::string error;
    ASSERT_TRUE(library.load(set, error)) << error;

    const Clip* clip = library.find("Walk");
    ASSERT_NE(clip, nullptr);
    EXPECT_TRUE(clip->looping);
    EXPECT_FLOAT_EQ(clip->length, 1.0f);

    const ClipAssetInfo* info = library.findInfo("Walk");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->rootBone, "Hips");
    EXPECT_EQ(library.find("Missing"), nullptr);
    EXPECT_EQ(library.findInfo("Missing"), nullptr);
    EXPECT_EQ(library.count(), 1);
}

TEST(AnimationClipLibrary, ReportsMissingAndInvalidFiles) {
    TempDir temp;
    AnimationSet set;
    ClipAssetInfo info = makeInfo("Walk", (temp.path() / "missing.ab").string());
    set.clips.push_back(info);

    AnimationClipLibrary library;
    std::string error;
    EXPECT_FALSE(library.load(set, error));
    EXPECT_NE(error.find("Walk"), std::string::npos) << error;

    const fs::path invalid = temp.path() / "invalid.ab";
    {
        std::ofstream invalidFile(invalid, std::ios::binary);
        invalidFile << "not an animation clip";
    }
    set.clips[0].file = invalid.string();
    EXPECT_FALSE(library.load(set, error));
    EXPECT_NE(error.find("Walk"), std::string::npos) << error;
}

TEST(AnimationTransitions, BuildsFromManifestAndMapsEvents) {
    TempDir temp;
    const std::string file = writeClip(temp, "Walk");

    AnimationSet set;
    set.clips.push_back(makeInfo("Walk", file));

    AnimationClipLibrary library;
    std::string error;
    ASSERT_TRUE(library.load(set, error)) << error;

    int footsteps = 0;
    EventCallbackMap callbacks;
    callbacks["Footstep.Left"] = [&footsteps] { ++footsteps; };

    AnimationTransitions transitions;
    ASSERT_TRUE(transitions.build(library, callbacks, error)) << error;

    ClipTransition* transition = transitions.find("Walk");
    ASSERT_NE(transition, nullptr);
    EXPECT_EQ(transition->clip, library.find("Walk"));
    EXPECT_FLOAT_EQ(transition->fadeDuration(), 0.15f);
    EXPECT_FLOAT_EQ(transition->speedValue, 1.25f);
    EXPECT_FLOAT_EQ(transition->normalizedStartTime, 0.2f);
    EXPECT_TRUE(transition->isLooping());
    EXPECT_EQ(transition->eventSequence.count(), 1);
    EXPECT_EQ(transition->eventSequence.indexOf("Footstep.Left"), 0);
    EXPECT_EQ(transition->eventSequence.indexOf("Footstep.Right"), -1);

    UnShared<ClipTransition> unshared(*transition);
    EXPECT_EQ(unshared.base().clip, transition->clip);
    EXPECT_EQ(unshared.base().eventSequence.count(), 1);
    EXPECT_NE(&unshared.base().eventSequence, &transition->eventSequence);

    ASSERT_NE(transitions.info("Walk"), nullptr);
    EXPECT_EQ(transitions.transition(0), transition);
    EXPECT_EQ(transitions.find("Missing"), nullptr);
}

TEST(AnimationTransitions, PlaysWithManifestRootBoneAndEvents) {
    Skeleton skeleton = makeSkeleton();
    TempDir temp;
    const std::string file = writeClip(temp, "Walk");

    AnimationSet set;
    set.clips.push_back(makeInfo("Walk", file));

    AnimationClipLibrary library;
    std::string error;
    ASSERT_TRUE(library.load(set, error)) << error;

    int footsteps = 0;
    EventCallbackMap callbacks;
    callbacks["Footstep.Left"] = [&footsteps] { ++footsteps; };

    AnimationTransitions transitions;
    ASSERT_TRUE(transitions.build(library, callbacks, error)) << error;

    HobiAnimator animator;
    animator.initialize(skeleton);

    HobiState* state = transitions.play(animator, "Walk", &error);
    ASSERT_NE(state, nullptr) << error;
    EXPECT_EQ(animator.playable().rootMotionRootBone(), 1);
    EXPECT_NEAR(state->normalizedTime(), 0.2f, 1e-5f);

    animator.update(0.1f);
    EXPECT_EQ(footsteps, 1);

    animator.update(0.8f);
    EXPECT_EQ(footsteps, 2);
}

TEST(AnimationTransitions, RejectsLoopingEventAtEnd) {
    TempDir temp;
    const std::string file = writeClip(temp, "Walk");

    AnimationSet set;
    ClipAssetInfo info = makeInfo("Walk", file);
    info.events = {ClipEventInfo{1.0f, "AtEnd"}};
    set.clips.push_back(info);

    AnimationClipLibrary library;
    std::string error;
    ASSERT_TRUE(library.load(set, error)) << error;

    EventCallbackMap callbacks;
    callbacks["AtEnd"] = [] {};

    AnimationTransitions transitions;
    EXPECT_FALSE(transitions.build(library, callbacks, error));
    EXPECT_NE(error.find("event"), std::string::npos) << error;
}

TEST(AnimationTransitions, BindsEmptyRootBoneAndRejectsUnknownBone) {
    Skeleton skeleton = makeSkeleton();
    TempDir temp;
    const std::string file = writeClip(temp, "Walk");

    AnimationSet set;
    ClipAssetInfo emptyBone = makeInfo("Wave", file);
    emptyBone.rootBone.clear();
    emptyBone.events.clear();
    set.clips.push_back(emptyBone);

    ClipAssetInfo badBone = makeInfo("Bad", file);
    badBone.rootBone = "Missing";
    badBone.events.clear();
    set.clips.push_back(badBone);

    AnimationClipLibrary library;
    std::string error;
    ASSERT_TRUE(library.load(set, error)) << error;

    AnimationTransitions transitions;
    ASSERT_TRUE(transitions.build(library, {}, error)) << error;

    HobiAnimator animator;
    animator.initialize(skeleton);

    ASSERT_NE(transitions.play(animator, "Wave", &error), nullptr) << error;
    EXPECT_EQ(animator.playable().rootMotionRootBone(), -1);

    EXPECT_EQ(transitions.play(animator, "Bad", &error), nullptr);
    EXPECT_NE(error.find("rootBone"), std::string::npos) << error;

    error.clear();
    EXPECT_EQ(transitions.play(animator, "Missing", &error), nullptr);
    EXPECT_NE(error.find("Missing"), std::string::npos) << error;
}

TEST(AnimationTransitions, KeepsEventContextsPerCharacter) {
    Skeleton skeleton = makeSkeleton();
    TempDir temp;
    const std::string file = writeClip(temp, "Walk");

    AnimationSet set;
    set.clips.push_back(makeInfo("Walk", file));

    AnimationClipLibrary library;
    std::string error;
    ASSERT_TRUE(library.load(set, error)) << error;

    int firstCount = 0;
    int secondCount = 0;
    EventCallbackMap firstCallbacks;
    EventCallbackMap secondCallbacks;
    firstCallbacks["Footstep.Left"] = [&firstCount] { ++firstCount; };
    secondCallbacks["Footstep.Left"] = [&secondCount] { ++secondCount; };

    AnimationTransitions first;
    AnimationTransitions second;
    ASSERT_TRUE(first.build(library, firstCallbacks, error)) << error;
    ASSERT_TRUE(second.build(library, secondCallbacks, error)) << error;

    HobiAnimator firstAnimator;
    HobiAnimator secondAnimator;
    firstAnimator.initialize(skeleton);
    secondAnimator.initialize(skeleton);

    ASSERT_NE(first.play(firstAnimator, "Walk", &error), nullptr) << error;
    ASSERT_NE(second.play(secondAnimator, "Walk", &error), nullptr) << error;

    firstAnimator.update(0.1f);
    secondAnimator.update(0.1f);
    EXPECT_EQ(firstCount, 1);
    EXPECT_EQ(secondCount, 1);

    firstAnimator.update(0.8f);
    EXPECT_EQ(firstCount, 2);
    EXPECT_EQ(secondCount, 1);
}

