#pragma once

#include <array>
#include <cstdint>

#include "anim/avatar/HumanBone.h"
#include "anim/avatar/MuscleTable.h"
#include "anim/core/Transform.h"

// 人形 Avatar：HumanBone → Skeleton 骨骼映射 + T-Pose + 肌肉轴对齐 + limits。
//
// 文档：docs/05-人形肌肉空间与重定向.md §3.1、§7
// 说明：preRot/postRot 用于把具体模型骨轴对齐到“肌肉轴坐标系”。
//      当前默认 identity；P4 的黄金数据标定会逐骨填充（见 docs/05 §8）。
namespace anim {

class Skeleton;

// Unity Mecanim 的肌肉轴是 (DoF[2], DoF[1], DoF[0]) 的 UVW 顺序，
// 并走 ZYRoll 组合；不能简单用 dqX*dqY*dqZ 代替。
struct HumanoidAxisBinding {
    int muscleId = -1;
    float sign = 1.0f;
};

// Unity mecanim::Axes（局部 pre/post + limits/sign/type）的只读快照。
// 用于精确复现 SkeletonGetDoF/SkeletonSetDoF/SkeletonAlign。
struct HumanoidBoneAxes {
    bool valid = false;
    Quat preQ{1.0f, 0.0f, 0.0f, 0.0f};
    Quat postQ{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 sign{1.0f, 1.0f, 1.0f};
    Vec3 limitMin{-1.0f, -1.0f, -1.0f};
    Vec3 limitMax{-1.0f, -1.0f, -1.0f};
    float length = 1.0f;
    // 0 = kFull(qtan), 1 = kZYRoll, 2 = kRollZY, 3 = kEulerXYZ
    int32_t type = 0;
};

struct HumanoidAvatar {
    std::array<int32_t, kHumanBoneCount> boneMap{};
    std::array<Transform, kHumanBoneCount> restLocal{};
    std::array<Quat, kHumanBoneCount> preRot{};
    std::array<Quat, kHumanBoneCount> postRot{};
    std::array<MuscleLimit, kMuscleCount> limits{};
    // 下标 = HumanBone，槽位 = Unity UVW 的 x/y/z；未设置时走旧的单轴路径。
    std::array<std::array<HumanoidAxisBinding, 3>, kHumanBoneCount> uvw{};
    std::array<bool, kHumanBoneCount> hasUvw{};
    std::array<HumanoidBoneAxes, kHumanBoneCount> axes{};
    float hipsHeight = 0.0f;

    // Unity HumanDescription twist distribution; defaults match mecanim::Human.
    float armTwist = 0.5f;
    float foreArmTwist = 0.5f;
    float upperLegTwist = 0.5f;
    float legTwist = 0.5f;

    HumanoidAvatar();

    bool hasBone(HumanBone bone) const noexcept;
    int boneIndex(HumanBone bone) const noexcept;

    const Transform& rest(HumanBone bone) const;
    const Quat& pre(HumanBone bone) const;
    const Quat& post(HumanBone bone) const;

    MuscleLimit limit(int muscleId) const;

    void setLimit(int muscleId, float minDegrees, float maxDegrees);
    void setBone(HumanBone bone, int skeletonBoneIndex);
    void setAxis(HumanBone bone, int slot, int muscleId, float sign);
};

// boneMap 下标为 HumanBone，值为 Skeleton 骨骼索引（-1 = 缺失）。
// hipsHeight <= 0 时由 T-Pose 自动计算（Hips 的世界 Y）。
HumanoidAvatar createHumanoidAvatar(
    const Skeleton& skeleton, const std::array<int32_t, kHumanBoneCount>& boneMap,
    float hipsHeight = 0.0f);

}  // namespace anim
