#include "anim/skeleton/Pose.h"

namespace anim {

void Pose::resetToRest(const Skeleton& skeleton) {
    local.resize(skeleton.bones.size());
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        local[i] = skeleton.bones[i].restLocal;
    }
}

}  // namespace anim
