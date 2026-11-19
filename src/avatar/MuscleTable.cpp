#include "anim/avatar/MuscleTable.h"

#include <cstring>

namespace anim {
namespace {

constexpr MuscleLimit limitFor(MuscleKind kind) noexcept {
    switch (kind) {
        case MuscleKind::Rotation: return {-90.0f, 90.0f};
        case MuscleKind::Twist: return {-90.0f, 90.0f};
        case MuscleKind::Stretch: return {-30.0f, 90.0f};
        case MuscleKind::Spread: return {-20.0f, 20.0f};
    }
    return {-90.0f, 90.0f};
}

#define ANIM_MUSCLE(name, bone, axis, kind) \
    {name, HumanBone::bone, MuscleAxis::axis, MuscleKind::kind, limitFor(MuscleKind::kind)}

// 顺序必须与 Unity HumanTrait.MuscleName 一致（见 docs/05 §2）。
constexpr MuscleEntry kMuscles[kMuscleCount] = {
    ANIM_MUSCLE("Spine Front-Back", Spine, X, Rotation),
    ANIM_MUSCLE("Spine Left-Right", Spine, Y, Rotation),
    ANIM_MUSCLE("Spine Twist Left-Right", Spine, Z, Twist),
    ANIM_MUSCLE("Chest Front-Back", Chest, X, Rotation),
    ANIM_MUSCLE("Chest Left-Right", Chest, Y, Rotation),
    ANIM_MUSCLE("Chest Twist Left-Right", Chest, Z, Twist),
    ANIM_MUSCLE("UpperChest Front-Back", UpperChest, X, Rotation),
    ANIM_MUSCLE("UpperChest Left-Right", UpperChest, Y, Rotation),
    ANIM_MUSCLE("UpperChest Twist Left-Right", UpperChest, Z, Twist),
    ANIM_MUSCLE("Neck Nod Down-Up", Neck, X, Rotation),
    ANIM_MUSCLE("Neck Tilt Left-Right", Neck, Y, Rotation),
    ANIM_MUSCLE("Neck Turn Left-Right", Neck, Z, Rotation),
    ANIM_MUSCLE("Head Nod Down-Up", Head, X, Rotation),
    ANIM_MUSCLE("Head Tilt Left-Right", Head, Y, Rotation),
    ANIM_MUSCLE("Head Turn Left-Right", Head, Z, Rotation),
    ANIM_MUSCLE("Left Eye Down-Up", LeftEye, X, Rotation),
    ANIM_MUSCLE("Left Eye In-Out", LeftEye, Y, Rotation),
    ANIM_MUSCLE("Right Eye Down-Up", RightEye, X, Rotation),
    ANIM_MUSCLE("Right Eye In-Out", RightEye, Y, Rotation),
    ANIM_MUSCLE("Jaw Close", Jaw, X, Rotation),
    ANIM_MUSCLE("Jaw Left-Right", Jaw, Y, Rotation),

    ANIM_MUSCLE("Left Upper Leg Front-Back", LeftUpperLeg, X, Rotation),
    ANIM_MUSCLE("Left Upper Leg In-Out", LeftUpperLeg, Y, Rotation),
    ANIM_MUSCLE("Left Upper Leg Twist In-Out", LeftUpperLeg, Z, Twist),
    ANIM_MUSCLE("Left Lower Leg Stretch", LeftLowerLeg, X, Stretch),
    ANIM_MUSCLE("Left Lower Leg Twist In-Out", LeftLowerLeg, Y, Twist),
    ANIM_MUSCLE("Left Foot Up-Down", LeftFoot, X, Rotation),
    ANIM_MUSCLE("Left Foot Twist In-Out", LeftFoot, Y, Twist),
    ANIM_MUSCLE("Left Toes Up-Down", LeftToes, X, Rotation),
    ANIM_MUSCLE("Right Upper Leg Front-Back", RightUpperLeg, X, Rotation),
    ANIM_MUSCLE("Right Upper Leg In-Out", RightUpperLeg, Y, Rotation),
    ANIM_MUSCLE("Right Upper Leg Twist In-Out", RightUpperLeg, Z, Twist),
    ANIM_MUSCLE("Right Lower Leg Stretch", RightLowerLeg, X, Stretch),
    ANIM_MUSCLE("Right Lower Leg Twist In-Out", RightLowerLeg, Y, Twist),
    ANIM_MUSCLE("Right Foot Up-Down", RightFoot, X, Rotation),
    ANIM_MUSCLE("Right Foot Twist In-Out", RightFoot, Y, Twist),
    ANIM_MUSCLE("Right Toes Up-Down", RightToes, X, Rotation),

    ANIM_MUSCLE("Left Shoulder Down-Up", LeftShoulder, X, Rotation),
    ANIM_MUSCLE("Left Shoulder Front-Back", LeftShoulder, Y, Rotation),
    ANIM_MUSCLE("Left Arm Down-Up", LeftUpperArm, X, Rotation),
    ANIM_MUSCLE("Left Arm Front-Back", LeftUpperArm, Y, Rotation),
    ANIM_MUSCLE("Left Arm Twist In-Out", LeftUpperArm, Z, Twist),
    ANIM_MUSCLE("Left Forearm Stretch", LeftLowerArm, X, Stretch),
    ANIM_MUSCLE("Left Forearm Twist In-Out", LeftLowerArm, Y, Twist),
    ANIM_MUSCLE("Left Hand Down-Up", LeftHand, X, Rotation),
    ANIM_MUSCLE("Left Hand In-Out", LeftHand, Y, Rotation),
    ANIM_MUSCLE("Right Shoulder Down-Up", RightShoulder, X, Rotation),
    ANIM_MUSCLE("Right Shoulder Front-Back", RightShoulder, Y, Rotation),
    ANIM_MUSCLE("Right Arm Down-Up", RightUpperArm, X, Rotation),
    ANIM_MUSCLE("Right Arm Front-Back", RightUpperArm, Y, Rotation),
    ANIM_MUSCLE("Right Arm Twist In-Out", RightUpperArm, Z, Twist),
    ANIM_MUSCLE("Right Forearm Stretch", RightLowerArm, X, Stretch),
    ANIM_MUSCLE("Right Forearm Twist In-Out", RightLowerArm, Y, Twist),
    ANIM_MUSCLE("Right Hand Down-Up", RightHand, X, Rotation),
    ANIM_MUSCLE("Right Hand In-Out", RightHand, Y, Rotation),

    ANIM_MUSCLE("LeftHand.Thumb.1 Stretched", LeftThumbProximal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Thumb.2 Stretched", LeftThumbIntermediate, X, Stretch),
    ANIM_MUSCLE("LeftHand.Thumb.3 Stretched", LeftThumbDistal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Thumb.Spread", LeftThumbProximal, Y, Spread),
    ANIM_MUSCLE("LeftHand.Index.1 Stretched", LeftIndexProximal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Index.2 Stretched", LeftIndexIntermediate, X, Stretch),
    ANIM_MUSCLE("LeftHand.Index.3 Stretched", LeftIndexDistal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Index.Spread", LeftIndexProximal, Y, Spread),
    ANIM_MUSCLE("LeftHand.Middle.1 Stretched", LeftMiddleProximal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Middle.2 Stretched", LeftMiddleIntermediate, X, Stretch),
    ANIM_MUSCLE("LeftHand.Middle.3 Stretched", LeftMiddleDistal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Middle.Spread", LeftMiddleProximal, Y, Spread),
    ANIM_MUSCLE("LeftHand.Ring.1 Stretched", LeftRingProximal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Ring.2 Stretched", LeftRingIntermediate, X, Stretch),
    ANIM_MUSCLE("LeftHand.Ring.3 Stretched", LeftRingDistal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Ring.Spread", LeftRingProximal, Y, Spread),
    ANIM_MUSCLE("LeftHand.Little.1 Stretched", LeftLittleProximal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Little.2 Stretched", LeftLittleIntermediate, X, Stretch),
    ANIM_MUSCLE("LeftHand.Little.3 Stretched", LeftLittleDistal, X, Stretch),
    ANIM_MUSCLE("LeftHand.Little.Spread", LeftLittleProximal, Y, Spread),

    ANIM_MUSCLE("RightHand.Thumb.1 Stretched", RightThumbProximal, X, Stretch),
    ANIM_MUSCLE("RightHand.Thumb.2 Stretched", RightThumbIntermediate, X, Stretch),
    ANIM_MUSCLE("RightHand.Thumb.3 Stretched", RightThumbDistal, X, Stretch),
    ANIM_MUSCLE("RightHand.Thumb.Spread", RightThumbProximal, Y, Spread),
    ANIM_MUSCLE("RightHand.Index.1 Stretched", RightIndexProximal, X, Stretch),
    ANIM_MUSCLE("RightHand.Index.2 Stretched", RightIndexIntermediate, X, Stretch),
    ANIM_MUSCLE("RightHand.Index.3 Stretched", RightIndexDistal, X, Stretch),
    ANIM_MUSCLE("RightHand.Index.Spread", RightIndexProximal, Y, Spread),
    ANIM_MUSCLE("RightHand.Middle.1 Stretched", RightMiddleProximal, X, Stretch),
    ANIM_MUSCLE("RightHand.Middle.2 Stretched", RightMiddleIntermediate, X, Stretch),
    ANIM_MUSCLE("RightHand.Middle.3 Stretched", RightMiddleDistal, X, Stretch),
    ANIM_MUSCLE("RightHand.Middle.Spread", RightMiddleProximal, Y, Spread),
    ANIM_MUSCLE("RightHand.Ring.1 Stretched", RightRingProximal, X, Stretch),
    ANIM_MUSCLE("RightHand.Ring.2 Stretched", RightRingIntermediate, X, Stretch),
    ANIM_MUSCLE("RightHand.Ring.3 Stretched", RightRingDistal, X, Stretch),
    ANIM_MUSCLE("RightHand.Ring.Spread", RightRingProximal, Y, Spread),
    ANIM_MUSCLE("RightHand.Little.1 Stretched", RightLittleProximal, X, Stretch),
    ANIM_MUSCLE("RightHand.Little.2 Stretched", RightLittleIntermediate, X, Stretch),
    ANIM_MUSCLE("RightHand.Little.3 Stretched", RightLittleDistal, X, Stretch),
    ANIM_MUSCLE("RightHand.Little.Spread", RightLittleProximal, Y, Spread),
};

#undef ANIM_MUSCLE

}  // namespace

const MuscleEntry& muscleEntry(int id) {
    return kMuscles[id < 0 ? 0 : (id >= kMuscleCount ? kMuscleCount - 1 : id)];
}

const MuscleEntry* muscleEntries() noexcept { return kMuscles; }

int findMuscleId(std::string_view name) noexcept {
    for (int i = 0; i < kMuscleCount; ++i) {
        if (name == kMuscles[i].name) return i;
    }
    return -1;
}

const char* muscleKindName(MuscleKind kind) noexcept {
    switch (kind) {
        case MuscleKind::Rotation: return "Rotation";
        case MuscleKind::Twist: return "Twist";
        case MuscleKind::Stretch: return "Stretch";
        case MuscleKind::Spread: return "Spread";
    }
    return "Unknown";
}

const char* muscleAxisName(MuscleAxis axis) noexcept {
    switch (axis) {
        case MuscleAxis::X: return "X";
        case MuscleAxis::Y: return "Y";
        case MuscleAxis::Z: return "Z";
    }
    return "?";
}

}  // namespace anim
