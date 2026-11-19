#pragma once

#include <cstddef>
#include <vector>

// 骨骼遮罩：按骨骼索引的权重（0..1）。空数组表示“全部为 1”。
//
// 文档：docs/06-Playable图.md §5、docs/07-Animancer门面.md（层）
namespace anim {

struct AvatarMask {
    std::vector<float> boneWeights;

    bool empty() const noexcept { return boneWeights.empty(); }
    std::size_t boneCount() const noexcept { return boneWeights.size(); }

    float weightFor(int boneIndex) const noexcept {
        if (boneWeights.empty()) return 1.0f;
        if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= boneWeights.size()) return 0.0f;
        return boneWeights[static_cast<std::size_t>(boneIndex)];
    }

    void resize(std::size_t count, float weight = 1.0f) { boneWeights.assign(count, weight); }
};

}  // namespace anim
