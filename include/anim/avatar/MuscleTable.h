#pragma once

#include <cstdint>
#include <string_view>

#include "anim/avatar/HumanBone.h"

// 95 个人形肌肉 DOF 的静态表（bone / axis / kind / name / 默认 limits）。
//
// 文档：docs/05-人形肌肉空间与重定向.md §2
// 名称与 .anim 中 `attribute:` 字符串完全一致（实测 Humanoid-Walk.anim）。
namespace anim {

inline constexpr int kMuscleCount = 95;

enum class MuscleAxis : uint8_t { X = 0, Y = 1, Z = 2 };

enum class MuscleKind : uint8_t {
    Rotation,  // 普通旋转（Front-Back / In-Out / Nod / Tilt / Turn / Close）
    Twist,     // Twist In-Out
    Stretch,   // LowerLeg / Forearm / 手指 Stretched
    Spread,    // 手指 Spread
};

struct MuscleLimit {
    float minDegrees = -90.0f;
    float maxDegrees = 90.0f;
};

struct MuscleEntry {
    const char* name = nullptr;
    HumanBone bone = HumanBone::Hips;
    MuscleAxis axis = MuscleAxis::X;
    MuscleKind kind = MuscleKind::Rotation;
    MuscleLimit defaultLimit{};
};

// 索引即 MuscleId（0..94，顺序与 Unity HumanTrait.MuscleName 一致）。
const MuscleEntry& muscleEntry(int id);
const MuscleEntry* muscleEntries() noexcept;

// 按 .anim 的 attribute 字符串查找 MuscleId；找不到返回 -1。
int findMuscleId(std::string_view name) noexcept;

const char* muscleKindName(MuscleKind kind) noexcept;
const char* muscleAxisName(MuscleAxis axis) noexcept;

}  // namespace anim
