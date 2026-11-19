#pragma once

#include <span>
#include <vector>

#include "anim/core/Math.h"
#include "anim/jobs/IAnimationJob.h"

namespace anim {

class DampingJob : public IAnimationJob {
public:
    int root = -1;
    std::vector<int> joints;
    std::vector<Vec3> localPositions;
    std::vector<Quat> localRotations;
    std::vector<Vec3> positions;
    std::vector<Vec3> velocities;

    float smoothTime = 0.15f;

    void setup(const Skeleton& skeleton, const Pose& pose, int rootBone,
               std::span<const int> jointBones);

    void processAnimation(Pose& pose, const Skeleton& skeleton, float deltaTime) override;

private:
    std::vector<Mat4> _scratch;
};

}  // namespace anim
