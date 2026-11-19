#include "anim/jobs/TwoBoneIK.h"

#include <cmath>

#include "../jobs/JobUtils.h"

namespace anim {

void TwoBoneIKJob::processAnimation(Pose& pose, const Skeleton& skeleton, float /*deltaTime*/) {
    const int boneCount = skeleton.boneCount();
    if (top < 0 || mid < 0 || low < 0) return;
    if (top >= boneCount || mid >= boneCount || low >= boneCount) return;

    const Quat aRotation = detail::worldRotation(skeleton, pose, top, &_scratch);
    const Quat bRotation = detail::worldRotation(skeleton, pose, mid, &_scratch);
    const Quat eRotation = useEffectorRotation
                               ? safeNormalize(effectorRotation)
                               : detail::worldRotation(skeleton, pose, low, &_scratch);

    const Vec3 aPosition = detail::worldPosition(skeleton, pose, top, &_scratch);
    const Vec3 bPosition = detail::worldPosition(skeleton, pose, mid, &_scratch);
    const Vec3 cPosition = detail::worldPosition(skeleton, pose, low, &_scratch);
    const Vec3 ePosition = effectorPosition;

    const Vec3 ab = bPosition - aPosition;
    const Vec3 bc = cPosition - bPosition;
    Vec3 ac = cPosition - aPosition;
    const Vec3 ae = ePosition - aPosition;

    const float abcAngle = detail::triangleAngle(glm::length(ac), ab, bc);
    const float abeAngle = detail::triangleAngle(glm::length(ae), ab, bc);
    const float angle = (abcAngle - abeAngle) * 57.29577951308232f;

    const Vec3 cross = glm::cross(ab, bc);
    Quat elbowRotation = kIdentityQuat;
    if (glm::dot(cross, cross) > kEpsilon) {
        const Vec3 axis = glm::normalize(cross);
        elbowRotation = glm::angleAxis(glm::radians(angle), axis);
    }

    detail::setWorldRotation(pose, skeleton, mid, elbowRotation * bRotation, &_scratch);

    const Vec3 newCPosition = detail::worldPosition(skeleton, pose, low, &_scratch);
    ac = newCPosition - aPosition;
    const Quat shoulderRotation = detail::fromToRotation(ac, ae);

    detail::setWorldRotation(pose, skeleton, top,
                             shoulderRotation * aRotation, &_scratch);
    detail::setWorldRotation(pose, skeleton, low, eRotation, &_scratch);
}

}  // namespace anim
