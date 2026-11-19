#pragma once

#include <array>

#include "anim/avatar/HumanBone.h"
#include "anim/avatar/MuscleTable.h"
#include "anim/clip/Clip.h"
#include "anim/core/Math.h"
#include "anim/core/Transform.h"

// 肌肉空间 ↔ 骨骼本地姿态。
//
// 文档：docs/05-人形肌肉空间与重定向.md §3、§4、§6
// 核心公式（有 Unity uvw 数据时）：
//   localRot[b] = restRot[b] * preRot[b] * ZYRoll(uvw) * postRot[b]
// ZYRoll2Quat(x,y,z) = normalize(1, x, y+x*z, z-x*y)。
// 没有 uvw 的旧 Avatar 才走 dqX*dqY*dqZ 兼容路径。
namespace anim {

class Skeleton;
struct HumanoidAvatar;
struct Pose;

struct MuscleValues {
    std::array<float, kMuscleCount> values{};

    float& operator[](int id) { return values[static_cast<std::size_t>(id)]; }
    float operator[](int id) const { return values[static_cast<std::size_t>(id)]; }
};

enum class HumanoidRootMode { Hips, MassCenter };

// 从 HumanoidClip 采样出的一帧（与目标骨架无关）。
struct HumanoidSample {
    MuscleValues muscles;
    HumanoidRootMode rootMode = HumanoidRootMode::Hips;

    Vec3 rootT{0.0f};
    Quat rootQ{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasRootT = false;
    bool hasRootQ = false;

    Vec3 leftFootT{0.0f};
    Vec3 rightFootT{0.0f};
    Quat leftFootQ{1.0f, 0.0f, 0.0f, 0.0f};
    Quat rightFootQ{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasFootT = false;
    bool hasFootQ = false;

    Vec3 leftHandT{0.0f};
    Vec3 rightHandT{0.0f};
    Quat leftHandQ{1.0f, 0.0f, 0.0f, 0.0f};
    Quat rightHandQ{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasHandT = false;
    bool hasHandQ = false;
};

// 采样肌肉/根/IK（P4 只实现肌肉与根，IK goal 留给 P5）。
HumanoidSample sampleHumanoidClip(const HumanoidClip& clip, float time);

// 肌肉 → 目标骨架 local Pose。未映射的骨骼保持 T-Pose。
void muscleSpaceToLocal(const MuscleValues& muscles, const HumanoidAvatar& avatar,
                        const Skeleton& skeleton, Pose& out);

// local Pose → HumanoidSample。当前支持带 UVW 的 avatar；零 limit 的
// muscle 不可逆（正向本来就会压成零角）。goal T/Q 按 Unity GetHumanPose
// 的 root-space + AddAxis 语义提取，脚目标含 Axes.length 的 foot bottom。
HumanoidSample humanoidLocalToSample(const HumanoidAvatar& avatar,
                                     const Skeleton& skeleton, const Pose& pose,
                                     float humanScale = 1.0f,
                                     HumanoidRootMode rootMode = HumanoidRootMode::Hips);

// 完整输入（含根运动）。RootT/RootQ 是 Hips 的完整 root-space local TRS，
// 会覆盖 rest 根变换；rootTransform 对应 Unity RetargetTo 的 arX。
void humanoidToLocal(const HumanoidSample& sample, const HumanoidAvatar& avatar,
                     const Skeleton& skeleton, Pose& out, float humanScale = 1.0f,
                     const Transform& rootTransform = Transform::identity());

}  // namespace anim
