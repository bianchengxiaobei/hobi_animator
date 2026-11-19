#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "anim/core/Hash.h"
#include "anim/core/Math.h"
#include "anim/core/Transform.h"

// 骨架（绑定姿态 + 层级 + 蒙皮逆绑定矩阵）。
//
// 文档：docs/04-骨架与位姿.md §1
// 约定：bones 按拓扑序排列（父索引严格小于子索引），根为 parent = -1。
namespace anim {

struct Bone {
    StringId name = kInvalidStringId;
    int32_t parent = -1;
    Transform restLocal{};              // 绑定姿态（T-Pose / 导入姿态）
    Mat4 inverseBind{1.0f};             // 蒙皮用逆绑定矩阵
};

class Skeleton {
public:
    std::vector<Bone> bones;

    // 追加骨骼。要求 parent < bones.size()（或 -1），且名字未重复。
    int addBone(StringId name, int32_t parent, const Transform& restLocal,
                const Mat4& inverseBind = Mat4(1.0f));

    int find(StringId name) const noexcept;
    int boneCount() const noexcept { return static_cast<int>(bones.size()); }
    bool empty() const noexcept { return bones.empty(); }
    int rootIndex() const noexcept;  // 第一个 parent < 0；没有则 -1

    const Transform& restLocal(int index) const;
    const Bone& bone(int index) const;

    void clear() noexcept;
    void reserve(std::size_t count);

    // 父索引严格小于子索引（拓扑序）。
    bool isTopological() const noexcept;

private:
    std::unordered_map<StringId, int32_t> _lookup;
};

}  // namespace anim
