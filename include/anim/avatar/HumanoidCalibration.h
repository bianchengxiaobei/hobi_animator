#pragma once

#include <array>
#include <string>
#include <vector>

#include "anim/avatar/HumanoidAvatar.h"
#include "anim/core/Math.h"

// 从 Unity HumanPoseHandler 的“单肌肉”黄金数据反解：
//   localRot = rest * pre * Rot(canonicalAxis, muscle * limit) * post
//
// 原项目本身不实现这层：AnimancerComponent/AnimancerPlayable 把 humanoid
// 交给 UnityEngine.Animator/Avatar（见 AnimancerPlayable.cs:491-497）。
// 因此这里以 Unity HumanPoseHandler 为 oracle，而不是复制闭源 native 数学。
//
// 对 value=0，Unity 应回到 T-Pose，因此 post = pre^-1。
// 对单轴采样，rest^-1 * localRot = pre * Rot(axis, angle) * pre^-1，
// 其旋转轴就是 pre * canonicalAxis；同一骨骼的 X/Y/Z 三轴可还原 pre。
//
// 文档：docs/05-人形肌肉空间与重定向.md §11.1、docs/13 §7
namespace anim {

struct MuscleCalibrationSample {
    int muscleId = -1;
    float value = 0.0f;
    Quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct HumanoidCalibrationData {
    // 下标是 HumanBone；rotation 来自 Unity Transform.localRotation。
    std::array<bool, kHumanBoneCount> hasRest{};
    std::array<Quat, kHumanBoneCount> restRotation{};
    // 可选：Unity HumanTrait.GetMuscleDefaultMin/Max（度数）。
    std::array<bool, kMuscleCount> hasDefaultLimit{};
    std::array<MuscleLimit, kMuscleCount> defaultLimits{};
    std::vector<MuscleCalibrationSample> samples;
};

struct HumanoidCalibrationReport {
    std::array<Quat, kHumanBoneCount> preRot{};
    std::array<Quat, kHumanBoneCount> postRot{};
    std::array<MuscleLimit, kMuscleCount> limits{};
    std::array<bool, kHumanBoneCount> calibratedBones{};
    std::array<bool, kMuscleCount> calibratedMuscles{};
    float maxErrorDegrees = 0.0f;
};

// 容差：单轴采样应能被公式精确复现；超过 toleranceDegrees 视为数据/轴序不一致。
bool calibrateHumanoid(const HumanoidCalibrationData& data,
                       HumanoidCalibrationReport& report, std::string& error,
                       float toleranceDegrees = 0.1f);

// 将标定结果写回 Avatar；未标定的 muscle 保留原 limits。
void applyHumanoidCalibration(HumanoidAvatar& avatar,
                              const HumanoidCalibrationReport& report);

}  // namespace anim
