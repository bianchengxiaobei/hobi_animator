#pragma once

#include "anim/core/Math.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

// 通用骨骼的根运动提取。
//
// 文档：docs/04-骨架与位姿.md §5、docs/10-根运动与Animation-Jobs.md
// 对齐原项目：RedirectRootMotionToTransform.cs:25-26
//   Target.position += Animator.deltaPosition;
//   Target.rotation *= Animator.deltaRotation;
namespace anim {

struct RootMotionDelta {
    Vec3 deltaPosition{0.0f, 0.0f, 0.0f};  // 世界空间位移差
    Quat deltaRotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasMotion = false;
};

// 取 rootIndex（默认自动找第一个根骨）在世界空间的帧间差。
RootMotionDelta extractRootMotion(const Pose& previous, const Pose& current,
                                  const Skeleton& skeleton, int rootIndex = -1);

}  // namespace anim
