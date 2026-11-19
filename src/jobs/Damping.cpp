#include "anim/jobs/Damping.h"

#include <algorithm>

#include "../jobs/JobUtils.h"

namespace anim {

void DampingJob::setup(const Skeleton& skeleton, const Pose& pose, int rootBone,
                       std::span<const int> jointBones) {
    root = rootBone;
    joints.assign(jointBones.begin(), jointBones.end());

    const std::size_t count = joints.size();
    localPositions.resize(count);
    localRotations.resize(count);
    positions.resize(count);
    velocities.assign(count, Vec3(0.0f));

    for (std::size_t i = 0; i < count; ++i) {
        const int bone = joints[i];
        localPositions[i] = pose.local[static_cast<std::size_t>(bone)].translation;
        localRotations[i] = safeNormalize(pose.local[static_cast<std::size_t>(bone)].rotation);
        positions[i] = detail::worldPosition(skeleton, pose, bone, &_scratch);
    }
}

void DampingJob::processAnimation(Pose& pose, const Skeleton& skeleton, float deltaTime) {
    if (joints.size() < 2) return;

    const int rootBone = root >= 0 ? root : skeleton.rootIndex();
    if (rootBone < 0) return;

    const Vec3 rootPosition = detail::worldPosition(skeleton, pose, rootBone, &_scratch);
    const Quat rootRotation = detail::worldRotation(skeleton, pose, rootBone, &_scratch);

    Vec3 parentPosition = rootPosition + rootRotation * localPositions[0];
    Quat parentRotation = rootRotation * localRotations[0];
    positions[0] = parentPosition;

    for (std::size_t i = 1; i < joints.size(); ++i) {
        const Vec3 newPosition = parentPosition + parentRotation * localPositions[i];

        Vec3 velocity = velocities[i];
        Vec3 damped = detail::smoothDamp(positions[i], newPosition, velocity,
                                         smoothTime, kInfinity, deltaTime);

        const Vec3 direction = damped - parentPosition;
        const float length = glm::length(direction);
        if (length > kEpsilon) {
            damped = parentPosition + direction / length * glm::length(localPositions[i]);
        } else {
            damped = parentPosition;
        }

        velocities[i] = velocity;
        positions[i] = damped;

        parentPosition = damped;
        parentRotation = parentRotation * localRotations[i];
    }

    parentRotation = rootRotation;
    for (std::size_t i = 0; i + 1 < joints.size(); ++i) {
        const Quat rotation = parentRotation * localRotations[i];
        const Vec3 direction = rotation * localPositions[i + 1];
        const Vec3 newDirection = positions[i + 1] - positions[i];

        const Quat fromTo = detail::fromToRotation(direction, newDirection);
        const Quat newRotation = fromTo * rotation;
        const Quat newLocal = safeNormalize(glm::inverse(parentRotation) * newRotation);

        pose.local[static_cast<std::size_t>(joints[i])].rotation = newLocal;
        parentRotation = newRotation;
    }
}

}  // namespace anim
