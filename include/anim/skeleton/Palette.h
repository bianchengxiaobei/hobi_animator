#pragma once

#include <vector>

#include "anim/core/Math.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

// 矩阵调色板输出。
//
// 文档：docs/04-骨架与位姿.md §4
// 约束：本层不包含任何渲染头文件（bgfx 只在 integration/samples 出现）。
namespace anim {

// 单根骨骼的世界矩阵（沿父链组合 local）。
Mat4 computeBoneWorldMatrix(const Skeleton& skeleton, const Pose& pose, int boneIndex,
                            std::vector<Mat4>* scratch = nullptr);

// 所有骨骼的世界矩阵（out[i] = parentWorld * localMatrix(i)）。
void computeWorldMatrices(const Skeleton& skeleton, const Pose& pose, std::vector<Mat4>& out);

// 蒙皮调色板：palette[i] = world[i] * bones[i].inverseBind。
void computePalette(const Skeleton& skeleton, const Pose& pose, std::vector<Mat4>& out);

}  // namespace anim
