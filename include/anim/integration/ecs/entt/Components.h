#pragma once

#include <vector>

#include "anim/hobi/HobiAnimator.h"
#include "anim/core/Math.h"
#include "anim/graph/PlayableHandle.h"
#include "anim/jobs/IAnimationJob.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/RootMotion.h"

// 可选的 EnTT 适配组件。核心库不包含 entt/entt.hpp，只有
// include/anim/integration/ecs/entt/Systems.h 依赖 EnTT。
//
// 文档：docs/16-任务调度与ECS集成.md §4
namespace anim::ecs {

// 一个实体对应一个角色。animator 由外部创建并保证生命周期长于 registry 中的组件。
struct HobiComponent {
    HobiAnimator* animator = nullptr;
    std::vector<PlayableHandle> jobHandles;
};

// 过程式动画 job。由 bindAnimationJobs 插入图输出链，顺序执行。
struct JobListComponent {
    std::vector<IAnimationJob*> jobs;
};

// 图求值后的位姿副本（可选；用于蒙皮/调试/其他系统读取）。
struct PoseComponent {
    Pose pose;
};

// 调色板副本（可选；上传 GPU 时使用）。
struct SkinningComponent {
    std::vector<Mat4> palette;
};

// 最近一帧提取出的根运动增量。
struct RootMotionComponent {
    RootMotionDelta delta;
};

}  // namespace anim::ecs
