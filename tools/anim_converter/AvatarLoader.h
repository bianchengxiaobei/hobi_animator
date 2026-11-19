#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "anim/avatar/HumanBone.h"
#include "anim/avatar/HumanoidAvatar.h"
#include "anim/avatar/MuscleTable.h"
#include "anim/core/Math.h"

namespace anim::converter {

struct AvatarConfig {
    std::string name;
    float hipsHeight = 0.0f;
    float armTwist = 0.5f;
    float foreArmTwist = 0.5f;
    float upperLegTwist = 0.5f;
    float legTwist = 0.5f;
    std::array<std::string, kHumanBoneCount> boneNames;
    struct LimitOverride {
        std::string muscleName;
        float minDegrees = 0.0f;
        float maxDegrees = 0.0f;
    };
    std::vector<LimitOverride> limits;

    // HumanBone -> local pre/post rotation from anim_calibrate. Missing entries stay
    // identity, so a hand-written avatar JSON without calibration still works.
    std::array<Quat, kHumanBoneCount> preRot{};
    std::array<Quat, kHumanBoneCount> postRot{};
    std::array<bool, kHumanBoneCount> hasPreRot{};
    std::array<bool, kHumanBoneCount> hasPostRot{};

    struct UvwBinding {
        std::string muscleName;
        float sign = 1.0f;
        float minDegrees = 0.0f;
        float maxDegrees = 0.0f;
    };
    // HumanBone -> Unity UVW slots (x/y/z in Human2SkeletonPose order).
    std::array<std::array<UvwBinding, 3>, kHumanBoneCount> uvw{};
    std::array<bool, kHumanBoneCount> hasUvw{};

    // Optional raw mecanim::Axes snapshot (source-generated avatars).
    std::array<HumanoidBoneAxes, kHumanBoneCount> axes{};
};

bool loadAvatarFile(const std::string& path, AvatarConfig& out, std::string& error);
bool loadAvatarJson(const std::string& json, AvatarConfig& out, std::string& error);

}  // namespace anim::converter
