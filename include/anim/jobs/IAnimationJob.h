#pragma once

#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

// Animation Job 接口（Unity IAnimationJob 的等价物）。
//
// 文档：docs/10-根运动与Animation-Jobs.md §5、docs/16-任务调度与ECS集成.md
// 说明：任务的“执行”由 IJobScheduler 负责（P5 实现）；本接口只描述“算什么”。
namespace anim {

class IJobScheduler;

struct IAnimationJob {
    virtual ~IAnimationJob() = default;

    virtual void processRootMotion(Pose& /*pose*/, const Skeleton& /*skeleton*/) {}
    virtual void processAnimation(Pose& pose, const Skeleton& skeleton, float deltaTime) = 0;
};

}  // namespace anim
