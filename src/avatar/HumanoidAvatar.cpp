#include "anim/avatar/HumanoidAvatar.h"

#include <vector>

#include "anim/core/Assert.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {

HumanoidAvatar::HumanoidAvatar() {
    boneMap.fill(-1);
    restLocal.fill(Transform::identity());
    preRot.fill(kIdentityQuat);
    postRot.fill(kIdentityQuat);
    for (int i = 0; i < kMuscleCount; ++i) limits[static_cast<std::size_t>(i)] = muscleEntry(i).defaultLimit;
}

bool HumanoidAvatar::hasBone(HumanBone bone) const noexcept {
    const int index = static_cast<int>(bone);
    return index >= 0 && index < kHumanBoneCount &&
           boneMap[static_cast<std::size_t>(index)] >= 0;
}

int HumanoidAvatar::boneIndex(HumanBone bone) const noexcept {
    const int index = static_cast<int>(bone);
    if (index < 0 || index >= kHumanBoneCount) return -1;
    return boneMap[static_cast<std::size_t>(index)];
}

const Transform& HumanoidAvatar::rest(HumanBone bone) const {
    return restLocal[static_cast<std::size_t>(humanBoneIndex(bone))];
}

const Quat& HumanoidAvatar::pre(HumanBone bone) const {
    return preRot[static_cast<std::size_t>(humanBoneIndex(bone))];
}

const Quat& HumanoidAvatar::post(HumanBone bone) const {
    return postRot[static_cast<std::size_t>(humanBoneIndex(bone))];
}

MuscleLimit HumanoidAvatar::limit(int muscleId) const {
    if (muscleId < 0 || muscleId >= kMuscleCount) return MuscleLimit{};
    return limits[static_cast<std::size_t>(muscleId)];
}

void HumanoidAvatar::setLimit(int muscleId, float minDegrees, float maxDegrees) {
    if (muscleId < 0 || muscleId >= kMuscleCount) return;
    limits[static_cast<std::size_t>(muscleId)] = {minDegrees, maxDegrees};
}

void HumanoidAvatar::setBone(HumanBone bone, int skeletonBoneIndex) {
    const int index = static_cast<int>(bone);
    if (index < 0 || index >= kHumanBoneCount) return;
    boneMap[static_cast<std::size_t>(index)] = skeletonBoneIndex;
}

void HumanoidAvatar::setAxis(HumanBone bone, int slot, int muscleId, float sign) {
    const int index = static_cast<int>(bone);
    if (index < 0 || index >= kHumanBoneCount || slot < 0 || slot >= 3) return;
    if (muscleId < 0 || muscleId >= kMuscleCount) return;
    uvw[static_cast<std::size_t>(index)][static_cast<std::size_t>(slot)] =
        HumanoidAxisBinding{muscleId, sign};
    hasUvw[static_cast<std::size_t>(index)] = true;
}

HumanoidAvatar createHumanoidAvatar(
    const Skeleton& skeleton, const std::array<int32_t, kHumanBoneCount>& boneMap,
    float hipsHeight) {
    HumanoidAvatar avatar;
    avatar.boneMap = boneMap;

    for (int i = 0; i < kHumanBoneCount; ++i) {
        const int bone = boneMap[static_cast<std::size_t>(i)];
        if (bone < 0) continue;
        ANIM_CHECK_MSG(bone < skeleton.boneCount(), "avatar bone index out of range");
        avatar.restLocal[static_cast<std::size_t>(i)] =
            skeleton.restLocal(bone);
    }

    if (hipsHeight <= 0.0f) {
        const int hips = avatar.boneIndex(HumanBone::Hips);
        if (hips >= 0) {
            std::vector<Transform> world(skeleton.bones.size(), Transform::identity());
            for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
                const Bone& bone = skeleton.bones[i];
                if (bone.parent < 0) {
                    world[i] = bone.restLocal;
                } else {
                    world[i] = Transform::compose(world[static_cast<std::size_t>(bone.parent)],
                                                  bone.restLocal);
                }
            }
            hipsHeight = world[static_cast<std::size_t>(hips)].translation.y;
        }
    }

    avatar.hipsHeight = hipsHeight;
    return avatar;
}

}  // namespace anim
