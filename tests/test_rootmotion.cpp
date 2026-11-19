#include <gtest/gtest.h>

#include <cmath>

#include "anim/hobi/rootmotion/RootMotion.h"
#include "anim/hobi/rootmotion/Targets.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/RootMotion.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeChain() {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});
    Transform child;
    child.translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.addBone(2, 0, child);
    return skeleton;
}

struct CountingTarget : IRootMotionTarget {
    int calls = 0;
    RootMotionDelta last;
    bool active = true;

    void applyRootMotion(const RootMotionDelta& delta) override {
        ++calls;
        last = delta;
    }

    bool enabled() const override { return active; }
};

struct FakeController : ICharacterControllerLike {
    Vec3 position{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    int moves = 0;
    int rotations = 0;

    void move(const Vec3& delta) override {
        position += delta;
        ++moves;
    }

    void rotate(const Quat& deltaRotation) override {
        rotation = safeNormalize(rotation * deltaRotation);
        ++rotations;
    }
};

struct FakeRigidbody : IRigidbodyLike {
    Vec3 currentPosition{0.0f};
    Quat currentRotation{1.0f, 0.0f, 0.0f, 0.0f};

    Vec3 position() const override { return currentPosition; }
    Quat rotation() const override { return currentRotation; }

    void movePosition(const Vec3& position) override { currentPosition = position; }
    void moveRotation(const Quat& rotation) override { currentRotation = rotation; }
};

}  // namespace

TEST(RootMotion, ExtractsWorldDelta) {
    const Skeleton skeleton = makeChain();
    Pose previous;
    previous.resizeLike(skeleton);
    previous.resetToRest(skeleton);

    Pose current = previous;
    current.local[0].translation = Vec3(1.0f, 2.0f, 3.0f);
    current.local[0].rotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));

    const RootMotionDelta delta = extractRootMotion(previous, current, skeleton);
    EXPECT_TRUE(delta.hasMotion);
    EXPECT_NEAR(delta.deltaPosition.x, 1.0f, 1e-5f);
    EXPECT_NEAR(delta.deltaPosition.y, 2.0f, 1e-5f);
    EXPECT_NEAR(delta.deltaPosition.z, 3.0f, 1e-5f);
    EXPECT_NEAR(std::abs(glm::dot(delta.deltaRotation,
                                  glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f)))),
                1.0f, 1e-5f);
}

TEST(RootMotion, RedirectAppliesToTransform) {
    Transform target;
    target.translation = Vec3(10.0f, 0.0f, 0.0f);
    target.rotation = glm::angleAxis(radians(30.0f), Vec3(0.0f, 0.0f, 1.0f));

    RootMotionToTransform toTransform(&target);
    RootMotionRedirect redirect;
    redirect.target = &toTransform;

    RootMotionDelta delta;
    delta.hasMotion = true;
    delta.deltaPosition = Vec3(1.0f, 2.0f, 3.0f);
    delta.deltaRotation = glm::angleAxis(radians(60.0f), Vec3(0.0f, 1.0f, 0.0f));

    redirect.update(delta);

    EXPECT_EQ(target.translation, Vec3(11.0f, 2.0f, 3.0f));
    const Quat expected = safeNormalize(
        glm::angleAxis(radians(30.0f), Vec3(0.0f, 0.0f, 1.0f)) * delta.deltaRotation);
    EXPECT_NEAR(std::abs(glm::dot(target.rotation, expected)), 1.0f, 1e-5f);
}

TEST(RootMotion, RedirectRespectsToggles) {
    CountingTarget target;
    RootMotionRedirect redirect;
    redirect.target = &target;

    RootMotionDelta delta;
    delta.hasMotion = true;
    delta.deltaPosition = Vec3(1.0f, 0.0f, 0.0f);

    redirect.applyRootMotion = false;
    redirect.update(delta);
    EXPECT_EQ(target.calls, 0);

    redirect.applyRootMotion = true;
    target.active = false;
    redirect.update(delta);
    EXPECT_EQ(target.calls, 0);

    target.active = true;
    redirect.update(delta);
    EXPECT_EQ(target.calls, 1);

    redirect.target = nullptr;
    redirect.update(delta);
    EXPECT_EQ(target.calls, 1);
}

TEST(RootMotion, PlayableAppliesDeltaEachFrame) {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});

    BoneLocalClip clip;
    clip.length = 1.0f;

    BoneChannel channel;
    channel.boneIndex = 0;
    channel.hasPosition = 1;
    channel.position[0].keys = {
        Keyframe{0.0f, 0.0f, 1.0f, 1.0f},
        Keyframe{1.0f, 1.0f, 1.0f, 1.0f},
    };
    clip.channels.push_back(channel);

    Transform targetTransform;
    RootMotionToTransform target(&targetTransform);

    HobiPlayable playable;
    playable.createOutput(skeleton);
    playable.setRootMotionTarget(&target);
    playable.play(clip);

    playable.evaluate(0.1f);
    EXPECT_EQ(targetTransform.translation, kZeroVec3);

    playable.evaluate(0.1f);
    EXPECT_NEAR(playable.rootMotionDelta().deltaPosition.x, 0.1f, 1e-3f);
    EXPECT_NEAR(targetTransform.translation.x, 0.1f, 1e-3f);

    playable.evaluate(0.1f);
    EXPECT_NEAR(targetTransform.translation.x, 0.2f, 1e-3f);
}

TEST(RootMotion, ControllerAndRigidbodyTargets) {
    RootMotionDelta delta;
    delta.hasMotion = true;
    delta.deltaPosition = Vec3(0.0f, 0.0f, 2.0f);
    delta.deltaRotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));

    FakeController controller;
    RootMotionToCharacterController toController(&controller);
    toController.applyRootMotion(delta);
    EXPECT_EQ(controller.position, Vec3(0.0f, 0.0f, 2.0f));
    EXPECT_EQ(controller.moves, 1);
    EXPECT_EQ(controller.rotations, 1);
    EXPECT_NEAR(std::abs(glm::dot(controller.rotation, delta.deltaRotation)), 1.0f, 1e-5f);

    FakeRigidbody rigidbody;
    rigidbody.currentPosition = Vec3(5.0f, 0.0f, 0.0f);
    RootMotionToRigidbody toRigidbody(&rigidbody);
    toRigidbody.applyRootMotion(delta);
    EXPECT_EQ(rigidbody.currentPosition, Vec3(5.0f, 0.0f, 2.0f));
    EXPECT_NEAR(std::abs(glm::dot(rigidbody.currentRotation, delta.deltaRotation)), 1.0f, 1e-5f);
}
