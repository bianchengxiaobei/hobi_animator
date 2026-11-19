#pragma once

#include <cstddef>
#include <vector>

#include "anim/core/Transform.h"
#include "anim/skeleton/Skeleton.h"

// 位姿缓冲（Unity AnimationStream 的等价物）：每根骨骼一个局部 TRS。
//
// 文档：docs/04-骨架与位姿.md §2
namespace anim {

struct Pose {
    std::vector<Transform> local;  // 与 Skeleton::bones 一一对应

    Pose() = default;

    std::size_t boneCount() const noexcept { return local.size(); }
    bool empty() const noexcept { return local.empty(); }
    bool matches(const Skeleton& skeleton) const noexcept {
        return local.size() == skeleton.bones.size();
    }

    void resizeLike(const Skeleton& skeleton) { local.resize(skeleton.bones.size()); }
    void resizeTo(std::size_t count) { local.resize(count); }
    void clear() noexcept { local.clear(); }

    // 全部骨骼恢复为绑定姿态。
    void resetToRest(const Skeleton& skeleton);

    Transform& operator[](std::size_t index) { return local[index]; }
    const Transform& operator[](std::size_t index) const { return local[index]; }
};

}  // namespace anim
