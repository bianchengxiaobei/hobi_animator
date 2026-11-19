#include "JobUtils.h"

#include <algorithm>
#include <cmath>

#include "anim/skeleton/Palette.h"

namespace anim::detail {
namespace {

Transform toTransform(const Mat4& matrix) {
    return Transform::fromMatrix(matrix);
}

}  // namespace

Transform worldTransform(const Skeleton& skeleton, const Pose& pose, int bone,
                         std::vector<Mat4>* scratch) {
    return toTransform(computeBoneWorldMatrix(skeleton, pose, bone, scratch));
}

Quat worldRotation(const Skeleton& skeleton, const Pose& pose, int bone,
                   std::vector<Mat4>* scratch) {
    return safeNormalize(worldTransform(skeleton, pose, bone, scratch).rotation);
}

Vec3 worldPosition(const Skeleton& skeleton, const Pose& pose, int bone,
                   std::vector<Mat4>* scratch) {
    return worldTransform(skeleton, pose, bone, scratch).translation;
}

void setWorldRotation(Pose& pose, const Skeleton& skeleton, int bone, const Quat& world,
                      std::vector<Mat4>* scratch) {
    Quat parentWorld = kIdentityQuat;
    const int parent = skeleton.bones[static_cast<std::size_t>(bone)].parent;
    if (parent >= 0) {
        parentWorld = worldRotation(skeleton, pose, parent, scratch);
    }

    pose.local[static_cast<std::size_t>(bone)].rotation =
        safeNormalize(glm::inverse(parentWorld) * safeNormalize(world));
}

Quat fromToRotation(const Vec3& from, const Vec3& to) {
    const float fromLength = glm::length(from);
    const float toLength = glm::length(to);
    if (fromLength <= kEpsilon || toLength <= kEpsilon) return kIdentityQuat;

    const Vec3 a = from / fromLength;
    const Vec3 b = to / toLength;

    const float dot = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (dot > 1.0f - 1e-6f) return kIdentityQuat;

    if (dot < -1.0f + 1e-6f) {
        Vec3 axis = glm::cross(a, Vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(axis, axis) < kEpsilon) axis = glm::cross(a, Vec3(1.0f, 0.0f, 0.0f));
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }

    const Vec3 axis = glm::cross(a, b);
    return glm::angleAxis(std::acos(dot), glm::normalize(axis));
}

float triangleAngle(float oppositeLength, const Vec3& a, const Vec3& b) {
    const float lengthA = glm::length(a);
    const float lengthB = glm::length(b);
    if (lengthA <= kEpsilon || lengthB <= kEpsilon) return 0.0f;

    const float cosine = glm::clamp(
        (lengthA * lengthA + lengthB * lengthB - oppositeLength * oppositeLength) /
            (lengthA * lengthB) / 2.0f,
        -1.0f, 1.0f);
    return std::acos(cosine);
}

Vec3 smoothDamp(const Vec3& current, const Vec3& target, Vec3& velocity,
                float smoothTime, float maxSpeed, float deltaTime) {
    const float omega = 2.0f / std::max(smoothTime, 0.0001f);
    const float x = omega * deltaTime;
    const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    Vec3 change = current - target;
    const Vec3 originalTarget = target;

    const float maxChange = maxSpeed * smoothTime;
    if (std::isfinite(maxChange)) {
        const float changeLength = glm::length(change);
        if (changeLength > maxChange && changeLength > kEpsilon) {
            change *= maxChange / changeLength;
        }
    }

    const Vec3 clampedTarget = current - change;
    const Vec3 temp = (velocity + change * omega) * deltaTime;
    velocity = (velocity - temp * omega) * exp;

    const Vec3 output = clampedTarget + (change + temp) * exp;
    if (glm::dot(originalTarget - current, output - originalTarget) > 0.0f) {
        velocity = deltaTime > 0.0f ? (originalTarget - current) / deltaTime : Vec3(0.0f);
        return originalTarget;
    }

    return output;
}

}  // namespace anim::detail
