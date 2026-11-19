#include "anim/jobs/SimpleLean.h"

#include "../jobs/JobUtils.h"

namespace anim {

void SimpleLeanJob::processAnimation(Pose& pose, const Skeleton& skeleton, float /*deltaTime*/) {
    if (bones.empty() || root < 0) return;

    const float perBone = angle / static_cast<float>(bones.size());
    const Quat rootRotation = detail::worldRotation(skeleton, pose, root, &_scratch);
    const Vec3 worldAxis = rootRotation * axis;
    if (glm::dot(worldAxis, worldAxis) <= kEpsilon) return;

    const Quat offset = glm::angleAxis(radians(perBone), glm::normalize(worldAxis));

    for (auto it = bones.rbegin(); it != bones.rend(); ++it) {
        const Quat world = detail::worldRotation(skeleton, pose, *it, &_scratch);
        detail::setWorldRotation(pose, skeleton, *it, offset * world, &_scratch);
    }
}

}  // namespace anim
