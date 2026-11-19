#pragma once

#include "anim/avatar/MuscleSpace.h"

// 可选的人形 goal → 两骨 IK 桥接。
//
// .anim 只保存 raw goal T/Q；Unity 的 ApplyFootIK/ApplyAnimatorIK 决定是否
// 以及以多大权重把 goal 喂给 FullBodySolve。这里提供一个显式权重入口，
// 使用 Unity Skeleton2BoneAdjustLength + 两骨几何求解腿/手链，并处理
// 末端目标旋转；Avatar 带 Axes 时走 ToAxes/FromAxes，否则回退 qtan。
// 另提供 applyHumanoidTwist / applyHumanoidLookAt 桥接，以及 retarget 用的
// fixHumanoidMidDoF / alignHumanoidBoneToReference / retargetHumanoidPose /
// retargetHumanoidFromPose（源 local Pose 的完整 RetargetFrom 流水线）。
namespace anim {

class Skeleton;
struct Pose;
struct HumanoidAvatar;

namespace detail {
Vec3 quatToEulerXYZ(const Quat& rotation);
Quat eulerXYZToQuat(const Vec3& euler);
Transform humanoidMassCenterRoot(const HumanoidAvatar& avatar,
                                 const Skeleton& skeleton, const Pose& pose);
Quat humanoidMassCenterReferenceRotation(const HumanoidAvatar& avatar,
                                         const Skeleton& skeleton);
void adjustHumanoidMissingBones(const HumanoidAvatar& avatar,
                                HumanoidSample& sample);
Transform humanoidMassCenterRoot(const HumanoidAvatar& avatar,
                                 const Skeleton& skeleton, const Pose& pose,
                                 const Quat& referenceRotation);
}  // namespace detail

struct HumanoidGoalWeights {
    float leftFootPosition = 0.0f;
    float leftFootRotation = 0.0f;
    float rightFootPosition = 0.0f;
    float rightFootRotation = 0.0f;
    float leftHandPosition = 0.0f;
    float leftHandRotation = 0.0f;
    float rightHandPosition = 0.0f;
    float rightHandRotation = 0.0f;

    // Unity Human defaults: ArmStretch = LegStretch = 0.05.
    float legStretch = 0.05f;
    float armStretch = 0.05f;
};

// Unity Human 默认 Twist 分配比例。
struct HumanoidTwistWeights {
    float armTwist = 0.5f;
    float foreArmTwist = 0.5f;
    float upperLegTwist = 0.5f;
    float legTwist = 0.5f;
};

// 对应 HumanPose.m_LookAtWeight：(clamp, body, head, eyes)。
struct HumanoidLookAtWeights {
    float clamp = 0.0f;
    float body = 0.0f;
    float head = 0.0f;
    float eyes = 0.0f;
};

// 在已经应用 RootT/RootQ 和肌肉姿态的 Pose 上应用四个 goal。
// goal T/Q 按 Unity HumanPose 的 root-space 语义解释，再由 Hips 的当前
// 全局变换转到世界空间；权重为 0 时完全跳过对应链。
void applyHumanoidGoals(const HumanoidSample& sample, const HumanoidAvatar& avatar,
                        const Skeleton& skeleton, Pose& pose,
                        const HumanoidGoalWeights& weights,
                        float humanScale = 1.0f);

// 对应 Unity TwistSolve：把上/下段骨的 twist 按比例分配到父骨，并保持
// 子骨世界旋转不变。无 Axes 数据时使用源码的 qtan 回退路径。
void applyHumanoidTwist(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                        Pose& pose);
void applyHumanoidTwist(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                        Pose& pose, const HumanoidTwistWeights& weights);

// 对应 FullBodySolve 的 look-at 段；targetPosition 是世界空间目标。
// 无 Axes 时以 Head 的世界 +Y 作为眼睛 forward，属于回退近似。
void applyHumanoidLookAt(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                         Pose& pose, const Vec3& targetPosition,
                         const HumanoidLookAtWeights& weights = {});

void applyHumanoidFullBodySolve(const HumanoidSample& sample,
                                const HumanoidAvatar& avatar,
                                const Skeleton& skeleton, Pose& pose,
                                const Vec3& lookAtTarget,
                                const HumanoidLookAtWeights& lookAtWeights,
                                const HumanoidGoalWeights& goalWeights,
                                float humanScale = 1.0f);

// Retarget 辅助：对应 HumanFixMidDoF，清除子骨投影 ZYRoll 的 Y 分量并由
// 父骨补偿，保持子骨世界旋转。
void fixHumanoidMidDoF(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                       Pose& pose, HumanBone parent, HumanBone child);

// Retarget 辅助：对应 HumanAlignSkeletonPose 的单骨部分，先按 Axes limits
// 规范化，再将子骨 world rotation 对齐到 referencePose，并写回 local。
void alignHumanoidBoneToReference(const HumanoidAvatar& avatar,
                                  const Skeleton& skeleton, Pose& pose,
                                  const Pose& referencePose, HumanBone child,
                                  HumanBone parent);

struct HumanoidRetargetSettings {
    int fixIterations = 5;
    HumanoidRootMode rootMode = HumanoidRootMode::Hips;
};

// RetargetFrom 的姿势修复阶段：肩/上臂/腿对齐 → mid DoF → 下臂/腿对齐 →
// 四个 endpoint 修复。referencePose 与 pose 使用同一目标骨架。
void retargetHumanoidPose(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                          Pose& pose, const Pose& referencePose,
                          const HumanoidRetargetSettings& settings = {});

// 完整 RetargetFrom：从源 local Pose 生成 HumanoidSample。
// 先按源 Pose 的 reference 做肩/肢体/endpoint 修复，再提取 muscles，
// 并从 reference 提取 root/goal；humanScale <= 0 时使用 avatar.hipsHeight。
HumanoidSample retargetHumanoidFromPose(
    const HumanoidAvatar& avatar, const Skeleton& skeleton, const Pose& pose,
    const HumanoidRetargetSettings& settings = {}, float humanScale = 0.0f);

// 跨骨架 local Pose retarget：源 Pose -> HumanoidSample -> 目标骨架。
// humanScale <= 0 时按 source/target hipsHeight 自动归一化并缩放。
void retargetHumanoidLocalPose(const HumanoidAvatar& sourceAvatar,
                               const Skeleton& sourceSkeleton,
                               const Pose& sourcePose,
                               const HumanoidAvatar& targetAvatar,
                               const Skeleton& targetSkeleton, Pose& targetPose,
                               float humanScale = 0.0f,
                               HumanoidRootMode rootMode = HumanoidRootMode::Hips);

}  // namespace anim
