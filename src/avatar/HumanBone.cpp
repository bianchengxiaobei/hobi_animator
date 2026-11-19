#include "anim/avatar/HumanBone.h"

namespace anim {
namespace {

const char* const kHumanBoneNames[kHumanBoneCount] = {
    "Hips",
    "LeftUpperLeg",
    "RightUpperLeg",
    "LeftLowerLeg",
    "RightLowerLeg",
    "LeftFoot",
    "RightFoot",
    "Spine",
    "Chest",
    "Neck",
    "Head",
    "LeftShoulder",
    "RightShoulder",
    "LeftUpperArm",
    "RightUpperArm",
    "LeftLowerArm",
    "RightLowerArm",
    "LeftHand",
    "RightHand",
    "LeftToes",
    "RightToes",
    "LeftEye",
    "RightEye",
    "Jaw",
    "LeftThumbProximal",
    "LeftThumbIntermediate",
    "LeftThumbDistal",
    "LeftIndexProximal",
    "LeftIndexIntermediate",
    "LeftIndexDistal",
    "LeftMiddleProximal",
    "LeftMiddleIntermediate",
    "LeftMiddleDistal",
    "LeftRingProximal",
    "LeftRingIntermediate",
    "LeftRingDistal",
    "LeftLittleProximal",
    "LeftLittleIntermediate",
    "LeftLittleDistal",
    "RightThumbProximal",
    "RightThumbIntermediate",
    "RightThumbDistal",
    "RightIndexProximal",
    "RightIndexIntermediate",
    "RightIndexDistal",
    "RightMiddleProximal",
    "RightMiddleIntermediate",
    "RightMiddleDistal",
    "RightRingProximal",
    "RightRingIntermediate",
    "RightRingDistal",
    "RightLittleProximal",
    "RightLittleIntermediate",
    "RightLittleDistal",
    "UpperChest",
};

}  // namespace

const char* humanBoneName(HumanBone bone) noexcept {
    const int index = static_cast<int>(bone);
    if (index < 0 || index >= kHumanBoneCount) return "Invalid";
    return kHumanBoneNames[index];
}

HumanBone humanBoneFromName(std::string_view name) noexcept {
    for (int i = 0; i < kHumanBoneCount; ++i) {
        if (name == kHumanBoneNames[i]) return static_cast<HumanBone>(i);
    }
    return HumanBone::Count;
}

}  // namespace anim
