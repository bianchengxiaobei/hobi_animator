#pragma once

#include <vector>

#include "anim/core/Math.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

namespace anim::detail {

Transform worldTransform(const Skeleton& skeleton, const Pose& pose, int bone,
                         std::vector<Mat4>* scratch = nullptr);

Quat worldRotation(const Skeleton& skeleton, const Pose& pose, int bone,
                   std::vector<Mat4>* scratch = nullptr);

Vec3 worldPosition(const Skeleton& skeleton, const Pose& pose, int bone,
                   std::vector<Mat4>* scratch = nullptr);

void setWorldRotation(Pose& pose, const Skeleton& skeleton, int bone, const Quat& world,
                      std::vector<Mat4>* scratch = nullptr);

Quat fromToRotation(const Vec3& from, const Vec3& to);

float triangleAngle(float oppositeLength, const Vec3& a, const Vec3& b);

Vec3 smoothDamp(const Vec3& current, const Vec3& target, Vec3& velocity,
                float smoothTime, float maxSpeed, float deltaTime);

}  // namespace anim::detail
