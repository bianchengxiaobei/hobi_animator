#pragma once

#include <vector>

#include "anim/core/Math.h"
#include "anim/jobs/IAnimationJob.h"

namespace anim {

class TwoBoneIKJob : public IAnimationJob {
public:
    int top = -1;
    int mid = -1;
    int low = -1;

    Vec3 effectorPosition{0.0f, 0.0f, 0.0f};
    Quat effectorRotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool useEffectorRotation = true;

    void setBones(int topBone, int midBone, int lowBone) noexcept {
        top = topBone;
        mid = midBone;
        low = lowBone;
    }

    void processAnimation(Pose& pose, const Skeleton& skeleton, float deltaTime) override;

private:
    std::vector<Mat4> _scratch;
};

}  // namespace anim
