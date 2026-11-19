#include <gtest/gtest.h>

#include <stdexcept>

#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {
Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    skeleton.addBone("Hips"_sid, 0, Transform::identity());
    skeleton.addBone("LeftLeg"_sid, 1, Transform::identity());
    return skeleton;
}
}  // namespace

TEST(Skeleton, BuildsHierarchyAndFindsBones) {
    Skeleton skeleton = makeSkeleton();

    ASSERT_EQ(skeleton.boneCount(), 3);
    EXPECT_EQ(skeleton.find("Root"_sid), 0);
    EXPECT_EQ(skeleton.find("Hips"_sid), 1);
    EXPECT_EQ(skeleton.find("LeftLeg"_sid), 2);
    EXPECT_EQ(skeleton.find("Missing"_sid), -1);
    EXPECT_EQ(skeleton.rootIndex(), 0);
    EXPECT_TRUE(skeleton.isTopological());

    EXPECT_EQ(skeleton.bone(2).parent, 1);
    EXPECT_EQ(skeleton.bone(1).parent, 0);
    EXPECT_EQ(skeleton.bone(0).parent, -1);
}

TEST(Skeleton, RejectsNonTopologicalParent) {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    EXPECT_THROW(skeleton.addBone("Bad"_sid, 5, Transform::identity()), std::logic_error);
}

TEST(Skeleton, RejectsDuplicateNames) {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    EXPECT_THROW(skeleton.addBone("Root"_sid, 0, Transform::identity()), std::logic_error);
}

TEST(Skeleton, ResetToRestCopiesRestTransform) {
    Skeleton skeleton;
    Transform rest;
    rest.translation = Vec3(1.0f, 2.0f, 3.0f);
    rest.rotation = glm::angleAxis(radians(30.0f), Vec3(0.0f, 1.0f, 0.0f));
    skeleton.addBone("Root"_sid, -1, rest);

    Pose pose;
    pose.resizeLike(skeleton);
    pose.resetToRest(skeleton);

    ASSERT_EQ(pose.boneCount(), 1u);
    EXPECT_EQ(pose[0].translation, rest.translation);
    EXPECT_NEAR(std::abs(glm::dot(pose[0].rotation, rest.rotation)), 1.0f, 1e-6f);
}

TEST(Skeleton, RestLookupChecksRange) {
    Skeleton skeleton = makeSkeleton();
    EXPECT_THROW(skeleton.restLocal(-1), std::logic_error);
    EXPECT_THROW(skeleton.restLocal(3), std::logic_error);
}
