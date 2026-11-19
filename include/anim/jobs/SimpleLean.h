#pragma once

#include <vector>

#include "anim/core/Math.h"
#include "anim/jobs/IAnimationJob.h"

namespace anim {

class SimpleLeanJob : public IAnimationJob {
public:
    int root = -1;
    std::vector<int> bones;
    Vec3 axis{0.0f, 0.0f, 1.0f};
    float angle = 0.0f;

    void processAnimation(Pose& pose, const Skeleton& skeleton, float deltaTime) override;

private:
    std::vector<Mat4> _scratch;
};

}  // namespace anim
