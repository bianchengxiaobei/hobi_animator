#include "anim/skeleton/Palette.h"

#include "anim/core/Assert.h"

namespace anim {

Mat4 computeBoneWorldMatrix(const Skeleton& skeleton, const Pose& pose, int boneIndex,
                            std::vector<Mat4>* scratch) {
    ANIM_CHECK_MSG(pose.local.size() == skeleton.bones.size(), "pose does not match skeleton");
    ANIM_CHECK_MSG(boneIndex >= 0 && boneIndex < skeleton.boneCount(), "bone index out of range");

    if (scratch != nullptr) {
        scratch->resize(skeleton.bones.size());
        for (std::size_t i = 0; i < scratch->size(); ++i) {
            (*scratch)[i] = Mat4(1.0f);
        }

        for (int i = 0; i <= boneIndex; ++i) {
            const int parent = skeleton.bones[static_cast<std::size_t>(i)].parent;
            const Mat4 localMatrix = pose.local[static_cast<std::size_t>(i)].toMatrix();
            (*scratch)[static_cast<std::size_t>(i)] =
                (parent >= 0 ? (*scratch)[static_cast<std::size_t>(parent)] : Mat4(1.0f)) *
                localMatrix;
        }
        return (*scratch)[static_cast<std::size_t>(boneIndex)];
    }

    // 无 scratch 时只沿父链组合（递归深度 = 骨骼层级深度）。
    std::vector<int> chain;
    for (int i = boneIndex; i >= 0; i = skeleton.bones[static_cast<std::size_t>(i)].parent) {
        chain.push_back(i);
    }

    Mat4 world(1.0f);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        world *= pose.local[static_cast<std::size_t>(*it)].toMatrix();
    }
    return world;
}

void computeWorldMatrices(const Skeleton& skeleton, const Pose& pose, std::vector<Mat4>& out) {
    ANIM_CHECK_MSG(pose.local.size() == skeleton.bones.size(), "pose does not match skeleton");
    ANIM_CHECK_MSG(skeleton.isTopological(),
                   "bones must be in topological order (parent before child)");

    out.resize(skeleton.bones.size());
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& bone = skeleton.bones[i];
        const Mat4 localMatrix = pose.local[i].toMatrix();
        out[i] = (bone.parent >= 0 ? out[static_cast<std::size_t>(bone.parent)] : Mat4(1.0f)) *
                 localMatrix;
    }
}

void computePalette(const Skeleton& skeleton, const Pose& pose, std::vector<Mat4>& out) {
    ANIM_CHECK_MSG(pose.local.size() == skeleton.bones.size(), "pose does not match skeleton");
    ANIM_CHECK_MSG(skeleton.isTopological(),
                   "bones must be in topological order (parent before child)");

    thread_local std::vector<Transform> world;
    world.resize(skeleton.bones.size());
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& bone = skeleton.bones[i];
        world[i] = bone.parent >= 0
                       ? Transform::compose(world[static_cast<std::size_t>(bone.parent)],
                                            pose.local[i])
                       : pose.local[i];
    }

    out.resize(world.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = world[i].toMatrix() * skeleton.bones[i].inverseBind;
    }
}

}  // namespace anim
