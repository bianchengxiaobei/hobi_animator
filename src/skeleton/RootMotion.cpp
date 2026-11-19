#include "anim/skeleton/RootMotion.h"

#include <cmath>
#include <vector>

#include "anim/core/Assert.h"
#include "anim/skeleton/Palette.h"

namespace anim {

RootMotionDelta extractRootMotion(const Pose& previous, const Pose& current,
                                  const Skeleton& skeleton, int rootIndex) {
    ANIM_CHECK_MSG(previous.local.size() == skeleton.bones.size(), "pose does not match skeleton");
    ANIM_CHECK_MSG(current.local.size() == skeleton.bones.size(), "pose does not match skeleton");

    if (rootIndex < 0) rootIndex = skeleton.rootIndex();
    ANIM_CHECK_MSG(rootIndex >= 0, "skeleton has no root bone");

    std::vector<Mat4> scratch;
    const Mat4 previousWorld = computeBoneWorldMatrix(skeleton, previous, rootIndex, &scratch);
    const Mat4 currentWorld = computeBoneWorldMatrix(skeleton, current, rootIndex, &scratch);

    // 从世界矩阵分解出 TRS，避免直接用带缩放的基向量构造四元数。
    const Transform previousTransform = Transform::fromMatrix(previousWorld);
    const Transform currentTransform = Transform::fromMatrix(currentWorld);

    RootMotionDelta delta;
    delta.deltaPosition = currentTransform.translation - previousTransform.translation;
    delta.deltaRotation = safeNormalize(currentTransform.rotation *
                                        glm::inverse(previousTransform.rotation));

    const float positionSqr = glm::dot(delta.deltaPosition, delta.deltaPosition);
    const float rotationDot = std::abs(glm::dot(delta.deltaRotation, kIdentityQuat));
    delta.hasMotion = positionSqr > kEpsilon || rotationDot < (1.0f - kEpsilon);

    return delta;
}

}  // namespace anim
