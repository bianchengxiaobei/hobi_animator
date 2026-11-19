#pragma once

#include <vector>

#include "anim/core/Span.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

// 位姿混合原语（L3）。
//
// 文档：docs/04-骨架与位姿.md §3
//
// 语义对照原项目：
//  - 权重归一化：ManualMixerState.NormalizeWeights(totalWeight)
//    （Internal/Mixer States/ManualMixerState.cs:1319）
//  - 总权重：ManualMixerState.CalculateTotalWeight
//    （Internal/Mixer States/ManualMixerState.cs:1276）
//  - 平移/缩放：按权重线性累加；旋转：加权累加 + 归一化（半球对齐）。
namespace anim {

float totalWeight(Span<const float> weights);

// 就地归一化：weights[i] /= total。total == 1 时直接返回（对齐原项目）。
void normalizeWeights(Span<float> weights);

// 单段线性插值：dst = lerp(a, b, t)。旋转用最短路径 slerp。
void lerpPose(const Pose& a, const Pose& b, float t, Pose& dst);

// 把累加器清零（translation/scale = 0，rotation = (0,0,0,0)）。
void resetAccumulator(const Skeleton& skeleton, Pose& accumulator);

// acc += src * weight。
// reference 非空时，用点积把 src.rotation 翻到同一半球（避免 q 与 -q 抵消）。
void accumulatePose(Pose& acc, const Pose& src, float weight, const Pose* reference = nullptr);

// 归一化累加器里的四元数（近零时退回单位四元数）。
void normalizePose(Pose& accumulator);

// 多路混合：权重最大者作为半球参考；累加后归一化。
// 输出的平移/缩放是 Σ w_i * v_i（不额外除总权重，由调用方保证权重和≈1）。
void blendPoses(Span<const Pose*> poses, Span<const float> weights, Pose& out);

}  // namespace anim
