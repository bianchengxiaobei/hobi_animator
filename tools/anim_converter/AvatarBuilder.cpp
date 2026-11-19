#include "AvatarBuilder.h"

#include <array>
#include <string>

#include "anim/avatar/HumanBone.h"
#include "anim/avatar/MuscleTable.h"

namespace anim::converter {

bool buildHumanoidAvatar(const SkeletonAsset& skeleton, const AvatarConfig& config,
                         HumanoidAvatar& out, std::string& error) {
    std::array<int32_t, kHumanBoneCount> boneMap{};
    boneMap.fill(-1);

    for (int i = 0; i < kHumanBoneCount; ++i) {
        const std::string& name = config.boneNames[static_cast<std::size_t>(i)];
        if (name.empty()) continue;

        const int bone = skeleton.resolvePath(name);
        if (bone < 0) {
            error = "avatar bone not found: " + name + " (" +
                    humanBoneName(static_cast<HumanBone>(i)) + ")";
            return false;
        }
        boneMap[static_cast<std::size_t>(i)] = bone;
    }

    if (boneMap[static_cast<std::size_t>(humanBoneIndex(HumanBone::Hips))] < 0) {
        error = "avatar requires a mapped Hips bone";
        return false;
    }

    out = createHumanoidAvatar(skeleton.skeleton, boneMap, config.hipsHeight);
    out.armTwist = config.armTwist;
    out.foreArmTwist = config.foreArmTwist;
    out.upperLegTwist = config.upperLegTwist;
    out.legTwist = config.legTwist;
    out.axes = config.axes;

    for (int i = 0; i < kHumanBoneCount; ++i) {
        if (config.hasPreRot[static_cast<std::size_t>(i)]) {
            out.preRot[static_cast<std::size_t>(i)] =
                config.preRot[static_cast<std::size_t>(i)];
        }
        if (config.hasPostRot[static_cast<std::size_t>(i)]) {
            out.postRot[static_cast<std::size_t>(i)] =
                config.postRot[static_cast<std::size_t>(i)];
        }
        if (!config.hasUvw[static_cast<std::size_t>(i)]) continue;
        for (int slot = 0; slot < 3; ++slot) {
            const AvatarConfig::UvwBinding& binding =
                config.uvw[static_cast<std::size_t>(i)]
                          [static_cast<std::size_t>(slot)];
            if (binding.muscleName.empty()) continue;
            const int muscle = findMuscleId(binding.muscleName);
            if (muscle < 0) {
                error = "unknown muscle in uvw: " + binding.muscleName;
                return false;
            }
            if (binding.minDegrees > binding.maxDegrees) {
                error = "invalid uvw limit for " + binding.muscleName;
                return false;
            }
            out.setLimit(muscle, binding.minDegrees, binding.maxDegrees);
            out.setAxis(static_cast<HumanBone>(i), slot, muscle, binding.sign);
        }
    }

    for (const AvatarConfig::LimitOverride& limit : config.limits) {
        const int muscle = findMuscleId(limit.muscleName);
        if (muscle < 0) {
            error = "unknown muscle in limitsOverride: " + limit.muscleName;
            return false;
        }
        if (limit.minDegrees > limit.maxDegrees) {
            error = "invalid limit override for " + limit.muscleName;
            return false;
        }
        out.setLimit(muscle, limit.minDegrees, limit.maxDegrees);
    }

    return true;
}

}  // namespace anim::converter
