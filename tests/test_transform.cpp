#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "anim/core/Transform.h"

using namespace anim;

namespace {
constexpr float kTolerance = 1e-5f;
}

TEST(Transform, MatchesMecanimXformPackedLayout) {
    EXPECT_EQ(sizeof(Transform), 48u);
    EXPECT_EQ(alignof(Transform), 16u);
    EXPECT_EQ(offsetof(Transform, translation), 0u);
    EXPECT_EQ(offsetof(Transform, rotation), 16u);
    EXPECT_EQ(offsetof(Transform, scale), 32u);
}

TEST(Transform, IdentityIsIdentityMatrix) {
    const Mat4 m = Transform::identity().toMatrix();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(m[c][r], (c == r) ? 1.0f : 0.0f, kTolerance);
        }
    }
}

TEST(Transform, ToMatrixMatchesExplicitTrs) {
    Transform trs;
    trs.translation = Vec3(1.0f, 2.0f, 3.0f);
    trs.rotation = glm::angleAxis(radians(30.0f), Vec3(0.0f, 1.0f, 0.0f));
    trs.scale = Vec3(2.0f, 3.0f, 4.0f);

    const Mat4 expected = glm::translate(Mat4(1.0f), trs.translation) *
                          glm::mat4_cast(trs.rotation) * glm::scale(Mat4(1.0f), trs.scale);
    const Mat4 actual = trs.toMatrix();

    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) EXPECT_NEAR(actual[c][r], expected[c][r], kTolerance);
}

TEST(Transform, FromMatrixRoundTrips) {
    Transform trs;
    trs.translation = Vec3(-4.0f, 0.5f, 7.0f);
    trs.rotation = glm::angleAxis(radians(-65.0f), glm::normalize(Vec3(1.0f, 2.0f, -1.0f)));
    trs.scale = Vec3(1.5f, 0.75f, 2.25f);

    const Transform roundTrip = Transform::fromMatrix(trs.toMatrix());

    EXPECT_NEAR(roundTrip.translation.x, trs.translation.x, kTolerance);
    EXPECT_NEAR(roundTrip.translation.y, trs.translation.y, kTolerance);
    EXPECT_NEAR(roundTrip.translation.z, trs.translation.z, kTolerance);
    EXPECT_NEAR(roundTrip.scale.x, trs.scale.x, kTolerance);
    EXPECT_NEAR(roundTrip.scale.y, trs.scale.y, kTolerance);
    EXPECT_NEAR(roundTrip.scale.z, trs.scale.z, kTolerance);

    // q 与 -q 表示同一旋转。
    const float dot = std::abs(glm::dot(roundTrip.rotation, trs.rotation));
    EXPECT_NEAR(dot, 1.0f, kTolerance);
}

TEST(Transform, ComposeAppliesParentScaleToChildTranslation) {
    Transform parent;
    parent.translation = Vec3(10.0f, 0.0f, 0.0f);
    parent.rotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    parent.scale = Vec3(2.0f, 2.0f, 2.0f);

    Transform child;
    child.translation = Vec3(0.0f, 0.0f, 1.0f);
    child.rotation = glm::angleAxis(radians(45.0f), Vec3(1.0f, 0.0f, 0.0f));
    child.scale = Vec3(0.5f, 0.5f, 0.5f);

    const Transform composed = Transform::compose(parent, child);

    // parent 旋转 90° 绕 Y：(0,0,2) -> (2,0,0)，再加 parent 平移 (10,0,0)。
    EXPECT_NEAR(composed.translation.x, 12.0f, kTolerance);
    EXPECT_NEAR(composed.translation.y, 0.0f, kTolerance);
    EXPECT_NEAR(composed.translation.z, 0.0f, kTolerance);
    EXPECT_NEAR(composed.scale.x, 1.0f, kTolerance);
    EXPECT_NEAR(composed.scale.y, 1.0f, kTolerance);
    EXPECT_NEAR(composed.scale.z, 1.0f, kTolerance);

    // 组合的矩阵应等于 parent 矩阵 * child 矩阵。
    const Mat4 expected = parent.toMatrix() * child.toMatrix();
    const Mat4 actual = composed.toMatrix();
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) EXPECT_NEAR(actual[c][r], expected[c][r], kTolerance);
}
