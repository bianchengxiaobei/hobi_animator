#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include "anim/animancer/AnimancerAnimator.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/jobs/AnimatedValue.h"
#include "anim/jobs/Damping.h"
#include "anim/jobs/JobScheduler.h"
#include "anim/jobs/ThreadPoolJobScheduler.h"
#include "anim/jobs/SimpleLean.h"
#include "anim/jobs/TwoBoneIK.h"
#include "anim/skeleton/Palette.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Transform worldTransform(const Skeleton& skeleton, const Pose& pose, int bone) {
    return Transform::fromMatrix(computeBoneWorldMatrix(skeleton, pose, bone));
}

void expectQuatNear(const Quat& a, const Quat& b, float epsilon = 1e-4f) {
    EXPECT_NEAR(std::abs(glm::dot(safeNormalize(a), safeNormalize(b))), 1.0f, epsilon);
}

float distance(const Vec3& a, const Vec3& b) { return glm::length(b - a); }

}  // namespace

TEST(Jobs, InlineSchedulerRunsImmediately) {
    InlineJobScheduler scheduler;
    EXPECT_EQ(scheduler.workerCount(), 1);

    int calls = 0;
    const JobHandle handle = scheduler.schedule([&] { ++calls; });
    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(calls, 1);

    scheduler.complete(handle);
    scheduler.waitAll();

    int begin = -1;
    int end = -1;
    scheduler.parallelFor(10, [&](std::size_t b, std::size_t e) {
        begin = static_cast<int>(b);
        end = static_cast<int>(e);
    });
    EXPECT_EQ(begin, 0);
    EXPECT_EQ(end, 10);
}

TEST(Jobs, TwoBoneIKReachesEffector) {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});
    Transform midRest;
    midRest.translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.addBone(2, 0, midRest);
    Transform lowRest;
    lowRest.translation = Vec3(0.5f, 1.0f, 0.0f);
    skeleton.addBone(3, 1, lowRest);

    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);

    const Vec3 topPosition = worldTransform(skeleton, pose, 0).translation;
    const float upperLength = distance(topPosition, worldTransform(skeleton, pose, 1).translation);
    const float lowerLength = distance(worldTransform(skeleton, pose, 1).translation,
                                      worldTransform(skeleton, pose, 2).translation);

    TwoBoneIKJob job;
    job.setBones(0, 1, 2);
    job.effectorPosition = Vec3(1.0f, 1.0f, 0.0f);
    job.effectorRotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));
    job.processAnimation(pose, skeleton, 1.0f / 60.0f);

    const Transform top = worldTransform(skeleton, pose, 0);
    const Transform mid = worldTransform(skeleton, pose, 1);
    const Transform low = worldTransform(skeleton, pose, 2);

    EXPECT_NEAR(distance(low.translation, job.effectorPosition), 0.0f, 1e-3f);
    EXPECT_NEAR(distance(top.translation, mid.translation), upperLength, 1e-3f);
    EXPECT_NEAR(distance(mid.translation, low.translation), lowerLength, 1e-3f);
    expectQuatNear(low.rotation, job.effectorRotation);
}

TEST(Jobs, TwoBoneIKWritesEffectorRotationOnDegenerateChain) {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});
    Transform child;
    child.translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.addBone(2, 0, child);
    skeleton.addBone(3, 1, child);

    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);

    TwoBoneIKJob job;
    job.setBones(0, 1, 2);
    job.effectorPosition = Vec3(1.0f, 0.0f, 0.0f);
    job.effectorRotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));
    job.processAnimation(pose, skeleton, 1.0f / 60.0f);

    expectQuatNear(worldTransform(skeleton, pose, 2).rotation,
                   job.effectorRotation);
}

TEST(Jobs, DampingConvergesAndPreservesLengths) {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});
    Transform segment;
    segment.translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.addBone(2, 0, segment);
    skeleton.addBone(3, 1, segment);
    skeleton.addBone(4, 2, segment);

    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);

    DampingJob job;
    const std::array<int, 3> joints{1, 2, 3};
    job.setup(skeleton, pose, 0, joints);
    ASSERT_EQ(job.positions.size(), 3u);
    EXPECT_NEAR(job.positions[2].y, 3.0f, 1e-5f);

    pose.local[0].translation = Vec3(1.0f, 0.0f, 0.0f);
    job.processAnimation(pose, skeleton, 1.0f / 60.0f);

    EXPECT_NEAR(job.positions[0].x, 1.0f, 1e-4f);
    EXPECT_GT(job.positions[2].x, 0.0f);
    EXPECT_LT(job.positions[2].x, 1.0f);
    EXPECT_NEAR(distance(job.positions[0], job.positions[1]), 1.0f, 1e-3f);
    EXPECT_NEAR(distance(job.positions[1], job.positions[2]), 1.0f, 1e-3f);

    for (int frame = 0; frame < 600; ++frame) {
        job.processAnimation(pose, skeleton, 1.0f / 60.0f);
    }
    EXPECT_NEAR(job.positions[2].x, 1.0f, 1e-2f);
    EXPECT_NEAR(distance(job.positions[0], job.positions[1]), 1.0f, 1e-3f);
    EXPECT_NEAR(distance(job.positions[1], job.positions[2]), 1.0f, 1e-3f);

    Skeleton zeroSkeleton;
    zeroSkeleton.addBone(1, -1, Transform{});
    zeroSkeleton.addBone(2, 0, segment);
    zeroSkeleton.addBone(3, 1, Transform{});
    zeroSkeleton.addBone(4, 2, segment);
    zeroSkeleton.addBone(5, 3, segment);

    Pose zeroPose;
    zeroPose.resizeLike(zeroSkeleton);
    zeroPose.resetToRest(zeroSkeleton);
    const Quat zeroMidRotation =
        glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    zeroPose.local[2].rotation = zeroMidRotation;

    DampingJob zeroJob;
    const std::array<int, 4> zeroJoints{1, 2, 3, 4};
    zeroJob.setup(zeroSkeleton, zeroPose, 0, zeroJoints);
    zeroPose.local[0].translation = Vec3(1.0f, 0.0f, 0.0f);
    zeroJob.processAnimation(zeroPose, zeroSkeleton, 1.0f / 60.0f);

    const Vec3 sourceDirection(0.0f, 1.0f, 0.0f);
    const Vec3 targetDirection =
        glm::normalize(zeroJob.positions[2] - zeroJob.positions[1]);
    const Vec3 cross = glm::cross(sourceDirection, targetDirection);
    ASSERT_GT(glm::length(cross), kEpsilon);
    const float cosine = glm::clamp(
        glm::dot(sourceDirection, targetDirection), -1.0f, 1.0f);
    const Quat fromTo =
        glm::angleAxis(std::acos(cosine), glm::normalize(cross));
    const Quat expected = fromTo * zeroMidRotation;
    expectQuatNear(zeroPose.local[2].rotation, expected);

    const Quat propagatedParent = expected;
    const Vec3 propagatedDirection = propagatedParent * Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 targetDirection2 =
        glm::normalize(zeroJob.positions[3] - zeroJob.positions[2]);
    const Vec3 cross2 = glm::cross(propagatedDirection, targetDirection2);
    ASSERT_GT(glm::length(cross2), kEpsilon);
    const float cosine2 = glm::clamp(
        glm::dot(propagatedDirection, targetDirection2), -1.0f, 1.0f);
    const Quat fromTo2 =
        glm::angleAxis(std::acos(cosine2), glm::normalize(cross2));
    const Quat expected2 =
        glm::inverse(propagatedParent) * fromTo2 * propagatedParent;
    expectQuatNear(zeroPose.local[3].rotation, expected2);
}

TEST(Jobs, SimpleLeanDistributesAngle) {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});
    Transform boneRest;
    boneRest.translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.addBone(2, 0, boneRest);
    skeleton.addBone(3, 1, boneRest);

    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);

    SimpleLeanJob job;
    job.root = 0;
    job.bones = {1, 2};
    job.axis = Vec3(0.0f, 1.0f, 0.0f);
    job.angle = 90.0f;
    job.processAnimation(pose, skeleton, 1.0f / 60.0f);

    const Quat half = glm::angleAxis(radians(45.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Quat full = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    expectQuatNear(worldTransform(skeleton, pose, 1).rotation, half);
    expectQuatNear(worldTransform(skeleton, pose, 2).rotation, full);
}

namespace {

struct RotateJob final : IAnimationJob {
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    void processAnimation(Pose& pose, const Skeleton&, float) override {
        pose.local[0].rotation = rotation;
    }
};

}  // namespace

TEST(Jobs, InsertAndRemoveOutputJob) {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});

    AnimancerPlayable playable;
    playable.createOutput(skeleton);

    RotateJob job;
    job.rotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    InlineJobScheduler scheduler;

    const PlayableHandle handle = playable.insertOutputJob(&job, &scheduler);
    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(playable.graph().output(), handle);

    playable.evaluate(1.0f / 60.0f);
    expectQuatNear(playable.outputPose().local[0].rotation, job.rotation);

    playable.removePlayable(handle);
    EXPECT_EQ(playable.node(handle), nullptr);
    EXPECT_EQ(playable.graph().output(), playable.playable());
}

TEST(Jobs, ParallelSchedulerMatchesInline) {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});

    AnimancerPlayable inlinePlayable;
    inlinePlayable.createOutput(skeleton);
    AnimancerPlayable parallelPlayable;
    parallelPlayable.createOutput(skeleton);

    RotateJob inlineJob;
    RotateJob parallelJob;
    inlineJob.rotation = glm::angleAxis(radians(37.0f), Vec3(0.0f, 1.0f, 0.0f));
    parallelJob.rotation = inlineJob.rotation;

    InlineJobScheduler inlineScheduler;
    ThreadPoolJobScheduler parallelScheduler(2);
    inlinePlayable.insertOutputJob(&inlineJob, &inlineScheduler);
    parallelPlayable.insertOutputJob(&parallelJob, &parallelScheduler);

    for (int frame = 0; frame < 8; ++frame) {
        inlinePlayable.evaluate(1.0f / 60.0f);
        parallelPlayable.evaluate(1.0f / 60.0f);
    }

    expectQuatNear(inlinePlayable.outputPose().local[0].rotation,
                   parallelPlayable.outputPose().local[0].rotation);
    expectQuatNear(parallelPlayable.outputPose().local[0].rotation,
                   inlineJob.rotation);
}

TEST(Jobs, RemoveMiddleJobReconnectsChain) {
    Skeleton skeleton;
    skeleton.addBone(1, -1, Transform{});

    AnimancerPlayable playable;
    playable.createOutput(skeleton);

    RotateJob first;
    RotateJob second;
    first.rotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    second.rotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));

    const PlayableHandle firstHandle = playable.insertOutputJob(&first);
    const PlayableHandle secondHandle = playable.insertOutputJob(&second);

    playable.removePlayable(firstHandle);
    EXPECT_EQ(playable.node(firstHandle), nullptr);
    EXPECT_EQ(playable.graph().output(), secondHandle);

    playable.evaluate(1.0f / 60.0f);
    expectQuatNear(playable.outputPose().local[0].rotation, second.rotation);
}

TEST(Jobs, AnimatedValuesReadCurves) {
    Curve curve;
    curve.keys = {
        Keyframe{0.0f, 0.0f, 0.0f, 0.0f},
        Keyframe{1.0f, 1.0f, 0.0f, 0.0f},
    };

    AnimatedFloat value;
    EXPECT_FALSE(value.hasCurve());
    EXPECT_FLOAT_EQ(value.sample(0.5f), 0.0f);

    value.curve = &curve;
    EXPECT_TRUE(value.hasCurve());
    EXPECT_NEAR(value.sample(0.5f), 0.5f, 1e-6f);

    AnimatedBool boolean;
    boolean.curve = &curve;
    EXPECT_FALSE(boolean.sample(0.25f));
    EXPECT_TRUE(boolean.sample(0.75f));

    AnimatedInt integer;
    integer.curve = &curve;
    EXPECT_EQ(integer.sample(0.25f), 0);
    EXPECT_EQ(integer.sample(0.75f), 1);
}

TEST(Jobs, AnimatedValuesReadPropertyChannels) {
    BoneLocalClip clip;
    PropertyChannel property;
    property.attribute = hashString("FootFall");
    property.curve.keys = {
        Keyframe{0.0f, 0.0f, 0.0f, 0.0f},
        Keyframe{1.0f, 1.0f, 0.0f, 0.0f},
    };
    clip.properties.push_back(property);

    AnimatedFloat value;
    EXPECT_TRUE(value.bindProperty(clip, "FootFall"));
    EXPECT_NEAR(value.sample(0.5f), 0.5f, 1e-6f);

    AnimatedBool boolean;
    EXPECT_TRUE(boolean.bindProperty(clip, "FootFall"));
    EXPECT_FALSE(boolean.sample(0.25f));
    EXPECT_TRUE(boolean.sample(0.75f));

    AnimatedInt integer;
    EXPECT_TRUE(integer.bindProperty(clip, "FootFall"));
    EXPECT_EQ(integer.sample(0.75f), 1);

    AnimatedFloat missing;
    EXPECT_FALSE(missing.bindProperty(clip, "Missing"));
    EXPECT_FALSE(missing.hasCurve());
    EXPECT_FLOAT_EQ(missing.sample(0.5f), 0.0f);
}
