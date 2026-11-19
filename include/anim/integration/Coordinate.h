#pragma once

#include "anim/core/Math.h"
#include "anim/skeleton/Pose.h"

// 手性转换（Unity 左手系 Y-up / +Z 前 ↔ 引擎右手系）。
//
// 文档：docs/12-引擎集成-bgfx.md §1
// 约定：Skeleton/Pose/Palette 全部处于引擎坐标系；这里只提供“镜像 Z”的
// 精确原语，由集成层在导入时一次性调用，而不是每帧转换。
namespace anim {

// S = diag(1, 1, -1, 1)。
inline Mat4 flipZMatrix() noexcept {
    Mat4 result(1.0f);
    result[2][2] = -1.0f;
    return result;
}

// 位置/方向向量镜像：z 取反。
inline Vec3 flipZ(const Vec3& value) noexcept { return Vec3(value.x, value.y, -value.z); }

// 旋转的共轭：R' = S R S，旋转轴 (x,y,z) 变为 (-x,-y,z)。
inline Quat flipZ(const Quat& value) noexcept {
    return Quat(value.w, -value.x, -value.y, value.z);
}

inline Transform flipZ(const Transform& value) noexcept {
    Transform result;
    result.translation = flipZ(value.translation);
    result.rotation = flipZ(value.rotation);
    result.scale = value.scale;
    return result;
}

// M' = S M S（位置自动 z 取反；线性部分保持右手旋转）。
inline Mat4 flipZ(const Mat4& value) noexcept {
    const Mat4 s = flipZMatrix();
    return s * value * s;
}

inline Pose flipZ(const Pose& pose) noexcept {
    Pose result;
    result.local.resize(pose.local.size());
    for (std::size_t i = 0; i < pose.local.size(); ++i) {
        result.local[i] = flipZ(pose.local[i]);
    }
    return result;
}

// 调色板/逆绑定矩阵的转换。
inline void flipZPalette(std::vector<Mat4>& palette) {
    for (Mat4& matrix : palette) matrix = flipZ(matrix);
}

}  // namespace anim
