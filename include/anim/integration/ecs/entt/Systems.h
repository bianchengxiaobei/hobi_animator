#pragma once

#include <entt/entt.hpp>

#include "Components.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/jobs/JobScheduler.h"

// EnTT 系统：实体遍历在 ECS 层，角色内部 job 仍由图输出链串行执行。
// 跨角色并行可由外部 IJobScheduler::parallelFor 驱动 animationGraphSystem 的
// 分片版本（见 docs/16 §5）。
namespace anim::ecs {

// 把 JobListComponent 中的 job 插入 HobiPlayable 的输出链。
// 应在角色创建后调用一次；重复调用会先移除旧 job，避免重复执行。
inline void bindAnimationJobs(entt::registry& registry) {
    auto view = registry.view<HobiComponent, JobListComponent>();
    for (entt::entity entity : view) {
        auto& component = view.get<HobiComponent>(entity);
        auto& list = view.get<JobListComponent>(entity);
        if (component.animator == nullptr) continue;

        HobiPlayable& playable = component.animator->playable();
        for (PlayableHandle handle : component.jobHandles) playable.removePlayable(handle);
        component.jobHandles.clear();

        for (IAnimationJob* job : list.jobs) {
            if (job != nullptr) component.jobHandles.push_back(playable.insertOutputJob(job));
        }
    }
}

// 推进每个角色的图。job 已在输出链中，因此顺序为：
// 采样 → 混合 → job → 根运动提取。
inline void animationGraphSystem(entt::registry& registry, float deltaTime) {
    auto view = registry.view<HobiComponent>();
    for (entt::entity entity : view) {
        HobiComponent& component = view.get<HobiComponent>(entity);
        if (component.animator != nullptr) component.animator->update(deltaTime);
    }
}

// 跨角色并行版本；不同角色的图/job 相互独立。
inline void animationGraphSystem(entt::registry& registry, IJobScheduler& scheduler,
                                 float deltaTime) {
    std::vector<HobiAnimator*> animators;
    auto view = registry.view<HobiComponent>();
    animators.reserve(view.size());
    for (entt::entity entity : view) {
        HobiComponent& component = view.get<HobiComponent>(entity);
        if (component.animator != nullptr) animators.push_back(component.animator);
    }

    scheduler.parallelFor(animators.size(), [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) animators[i]->update(deltaTime);
    });
}

// 把 animator 的输出复制到可选组件，便于渲染/其他 ECS 系统读取。
inline void syncAnimationOutputs(entt::registry& registry) {
    auto view = registry.view<HobiComponent>();
    for (entt::entity entity : view) {
        HobiComponent& component = view.get<HobiComponent>(entity);
        if (component.animator == nullptr) continue;

        if (PoseComponent* pose = registry.try_get<PoseComponent>(entity)) {
            pose->pose = component.animator->playable().outputPose();
        }
        if (SkinningComponent* skinning = registry.try_get<SkinningComponent>(entity)) {
            const std::vector<Mat4>& palette = component.animator->palette();
            skinning->palette.assign(palette.begin(), palette.end());
        }
        if (RootMotionComponent* rootMotion = registry.try_get<RootMotionComponent>(entity)) {
            rootMotion->delta = component.animator->consumeRootMotion();
        }
    }
}

}  // namespace anim::ecs
