#include <gtest/gtest.h>

#include "anim/clip/Clip.h"
#include "anim/core/Hash.h"
#include "anim/integration/ecs/entt/Components.h"
#include "anim/integration/ecs/entt/Systems.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/jobs/JobScheduler.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

struct OffsetJob final : IAnimationJob {
    void processAnimation(Pose& pose, const Skeleton&, float) override {
        if (!pose.local.empty()) pose.local[0].translation.y += 1.0f;
    }
};

}  // namespace

TEST(Entt, BindsJobsAndSyncsOutputs) {
    Skeleton skeleton;
    skeleton.addBone(hashString("Root"), -1, Transform{});

    AnimancerAnimator animator;
    animator.initialize(skeleton);
    animator.playable().defaultFadeDuration = 0.0f;

    BoneLocalClip clip;
    clip.length = 1.0f;
    ASSERT_NE(animator.play(clip), nullptr);

    OffsetJob job;
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<ecs::AnimancerComponent>(entity, ecs::AnimancerComponent{&animator});
    registry.emplace<ecs::JobListComponent>(entity, ecs::JobListComponent{{&job}});
    registry.emplace<ecs::PoseComponent>(entity);
    registry.emplace<ecs::SkinningComponent>(entity);
    registry.emplace<ecs::RootMotionComponent>(entity);

    ecs::bindAnimationJobs(registry);
    InlineJobScheduler scheduler;
    ecs::animationGraphSystem(registry, scheduler, 0.1f);
    ecs::syncAnimationOutputs(registry);

    ASSERT_EQ(registry.get<ecs::PoseComponent>(entity).pose.local.size(), 1u);
    EXPECT_NEAR(registry.get<ecs::PoseComponent>(entity).pose.local[0].translation.y, 1.0f, 1e-5f);
    ASSERT_EQ(registry.get<ecs::SkinningComponent>(entity).palette.size(), 1u);
    EXPECT_FLOAT_EQ(registry.get<ecs::RootMotionComponent>(entity).delta.deltaPosition.y, 0.0f);
}
