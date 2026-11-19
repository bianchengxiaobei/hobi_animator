#include <gtest/gtest.h>

#include <vector>

#include "anim/skeleton/Palette.h"

using namespace anim;

namespace {

struct Rig {
    Skeleton skeleton;
    Pose pose;
};

Rig makeRig() {
    Rig rig;

    Transform rootLocal;
    rootLocal.translation = Vec3(1.0f, 0.0f, 0.0f);
    rootLocal.rotation = glm::angleAxis(radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    Mat4 rootInverse = glm::translate(Mat4(1.0f), Vec3(-1.0f, 0.0f, 0.0f));

    Transform hipsLocal;
    hipsLocal.translation = Vec3(0.0f, 0.0f, 2.0f);
    hipsLocal.rotation = glm::angleAxis(radians(45.0f), Vec3(1.0f, 0.0f, 0.0f));
    Mat4 hipsInverse = glm::translate(Mat4(1.0f), Vec3(0.0f, 0.0f, -2.0f));

    Transform childLocal;
    childLocal.translation = Vec3(0.0f, 1.0f, 0.0f);
    childLocal.scale = Vec3(1.0f, 2.0f, 3.0f);

    rig.skeleton.addBone("Root"_sid, -1, rootLocal, rootInverse);
    rig.skeleton.addBone("Hips"_sid, 0, hipsLocal, hipsInverse);
    rig.skeleton.addBone("Child"_sid, 1, childLocal, Mat4(1.0f));

    rig.pose.resizeLike(rig.skeleton);
    rig.pose.resetToRest(rig.skeleton);
    return rig;
}

void expectMatrixNear(const Mat4& actual, const Mat4& expected, float tolerance = 1e-5f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(actual[c][r], expected[c][r], tolerance) << "column " << c << " row " << r;
        }
    }
}

Transform referenceWorld(const Pose& pose, int index, const Skeleton& skeleton) {
    Transform world = pose[index];
    for (int parent = skeleton.bone(index).parent; parent >= 0; parent = skeleton.bone(parent).parent) {
        world = Transform::compose(pose[parent], world);
    }
    return world;
}

}  // namespace

TEST(Palette, WorldMatricesMatchHierarchyComposition) {
    const Rig rig = makeRig();

    std::vector<Mat4> world;
    computeWorldMatrices(rig.skeleton, rig.pose, world);

    ASSERT_EQ(world.size(), 3u);
    for (int i = 0; i < rig.skeleton.boneCount(); ++i) {
        expectMatrixNear(world[static_cast<std::size_t>(i)],
                         referenceWorld(rig.pose, i, rig.skeleton).toMatrix());
    }
}

TEST(Palette, SingleBoneWorldMatchesFullTraversal) {
    const Rig rig = makeRig();

    std::vector<Mat4> world;
    computeWorldMatrices(rig.skeleton, rig.pose, world);

    std::vector<Mat4> scratch;
    for (int i = 0; i < rig.skeleton.boneCount(); ++i) {
        expectMatrixNear(computeBoneWorldMatrix(rig.skeleton, rig.pose, i, &scratch),
                         world[static_cast<std::size_t>(i)]);
        // 不传 scratch 的路径也必须一致。
        expectMatrixNear(computeBoneWorldMatrix(rig.skeleton, rig.pose, i, nullptr),
                         world[static_cast<std::size_t>(i)]);
    }
}

TEST(Palette, SkinningPaletteMultipliesInverseBind) {
    const Rig rig = makeRig();

    std::vector<Mat4> world;
    computeWorldMatrices(rig.skeleton, rig.pose, world);

    std::vector<Mat4> palette;
    computePalette(rig.skeleton, rig.pose, palette);

    ASSERT_EQ(palette.size(), 3u);
    for (std::size_t i = 0; i < palette.size(); ++i) {
        expectMatrixNear(palette[i], world[i] * rig.skeleton.bones[i].inverseBind);
    }
}

TEST(Palette, DetectsPoseSizeMismatch) {
    Rig rig = makeRig();
    rig.pose.local.pop_back();

    std::vector<Mat4> world;
    EXPECT_THROW(computeWorldMatrices(rig.skeleton, rig.pose, world), std::logic_error);
    EXPECT_THROW(computeBoneWorldMatrix(rig.skeleton, rig.pose, 0), std::logic_error);
}
