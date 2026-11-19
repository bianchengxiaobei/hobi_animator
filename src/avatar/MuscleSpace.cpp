#include "anim/avatar/MuscleSpace.h"

#include <algorithm>
#include <cmath>

#include "anim/avatar/HumanoidAvatar.h"
#include "anim/avatar/HumanoidIK.h"
#include "anim/core/Assert.h"
#include "anim/skeleton/Palette.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {
namespace {

float evaluateOr(const Curve& curve, float time, float fallback) {
    return curve.empty() ? fallback : curve.evaluate(time);
}

Transform inverseRigid(const Transform& transform) {
    Transform result;
    result.rotation = glm::conjugate(transform.rotation);
    result.translation = -(result.rotation * transform.translation);
    return result;
}

float muscleAngle(const MuscleValues& muscles, int id, const HumanoidAvatar& avatar) {
    const MuscleLimit limit = avatar.limit(id);
    const float value = std::clamp(muscles[id], -1.0f, 1.0f);
    return value >= 0.0f ? value * limit.maxDegrees : value * (-limit.minDegrees);
}

Vec3 axisVector(MuscleAxis axis) noexcept {
    switch (axis) {
        case MuscleAxis::X: return Vec3(1.0f, 0.0f, 0.0f);
        case MuscleAxis::Y: return Vec3(0.0f, 1.0f, 0.0f);
        case MuscleAxis::Z: return Vec3(0.0f, 0.0f, 1.0f);
    }
    return Vec3(1.0f, 0.0f, 0.0f);
}

float halfTangent(float radians) noexcept {
    constexpr float limit = kPi * 0.5f - 1e-6f;
    return std::tan(std::clamp(radians * 0.5f, -limit, limit));
}

// Port of mecanim::math::ZYRoll2Quat, Unity's humanoid muscle composition.
Quat zyRollToQuat(float x, float y, float z) noexcept {
    return safeNormalize(Quat(1.0f, x, y + x * z, z - x * y));
}

}  // namespace

void muscleSpaceToLocal(const MuscleValues& muscles, const HumanoidAvatar& avatar,
                        const Skeleton& skeleton, Pose& out) {
    out.resizeLike(skeleton);
    out.resetToRest(skeleton);

    std::array<Quat, kHumanBoneCount> deltas;
    deltas.fill(kIdentityQuat);

    // Old path for avatars without Unity UVW axis data.
    for (int id = 0; id < kMuscleCount; ++id) {
        const MuscleEntry& entry = muscleEntry(id);
        const int boneIndex = humanBoneIndex(entry.bone);
        if (avatar.boneIndex(entry.bone) < 0 ||
            avatar.hasUvw[static_cast<std::size_t>(boneIndex)]) {
            continue;
        }

        const float angle = muscleAngle(muscles, id, avatar);
        if (angle == 0.0f) continue;

        const Quat rotation =
            glm::angleAxis(radians(angle), axisVector(entry.axis));
        Quat& delta = deltas[static_cast<std::size_t>(boneIndex)];
        delta = delta * rotation;
    }

    for (int i = 0; i < kHumanBoneCount; ++i) {
        const HumanBone bone = static_cast<HumanBone>(i);
        const int skeletonBone = avatar.boneIndex(bone);
        if (skeletonBone < 0) continue;

        Quat delta = deltas[static_cast<std::size_t>(i)];
        if (avatar.hasUvw[static_cast<std::size_t>(i)]) {
            std::array<float, 3> uvw{0.0f, 0.0f, 0.0f};
            for (int slot = 0; slot < 3; ++slot) {
                const HumanoidAxisBinding binding =
                    avatar.uvw[static_cast<std::size_t>(i)]
                              [static_cast<std::size_t>(slot)];
                if (binding.muscleId < 0) continue;
                const float value =
                    std::clamp(muscles[binding.muscleId], -1.0f, 1.0f);
                const MuscleLimit limit = avatar.limit(binding.muscleId);
                const float angle = value >= 0.0f ? value * limit.maxDegrees
                                                  : value * (-limit.minDegrees);
                uvw[static_cast<std::size_t>(slot)] =
                    halfTangent(radians(angle)) * binding.sign;
            }
            delta = zyRollToQuat(uvw[0], uvw[1], uvw[2]);
        }

        Transform& transform = out.local[static_cast<std::size_t>(skeletonBone)];
        const Transform& rest = avatar.rest(bone);
        transform.translation = rest.translation;
        transform.scale = rest.scale;
        transform.rotation = safeNormalize(rest.rotation * avatar.pre(bone) * delta *
                                           avatar.post(bone));
    }
}

void humanoidToLocal(const HumanoidSample& sample, const HumanoidAvatar& avatar,
                     const Skeleton& skeleton, Pose& out, float humanScale,
                     const Transform& rootTransform) {
    HumanoidSample adjusted = sample;
    detail::adjustHumanoidMissingBones(avatar, adjusted);
    muscleSpaceToLocal(adjusted.muscles, avatar, skeleton, out);

    const int hips = avatar.boneIndex(HumanBone::Hips);
    if (hips < 0) return;

    Transform& hipsTransform = out.local[static_cast<std::size_t>(hips)];
    if (adjusted.rootMode == HumanoidRootMode::MassCenter) {
        const float scale =
            std::abs(humanScale) > kEpsilon ? humanScale : 1.0f;
        const Quat reference =
            detail::humanoidMassCenterReferenceRotation(avatar, skeleton);
        const Transform currentMass =
            detail::humanoidMassCenterRoot(avatar, skeleton, out, reference);
        Transform targetRoot;
        targetRoot.translation = adjusted.hasRootT
                                     ? adjusted.rootT * scale
                                     : currentMass.translation;
        targetRoot.rotation = adjusted.hasRootQ
                                  ? safeNormalize(adjusted.rootQ)
                                  : currentMass.rotation;
        const Transform desired =
            Transform::compose(rootTransform, targetRoot);
        const Transform delta =
            Transform::compose(desired, inverseRigid(currentMass));
        const Transform hipsWorld =
            Transform::fromMatrix(computeBoneWorldMatrix(skeleton, out, hips));
        const Transform adjustedWorld = Transform::compose(delta, hipsWorld);
        const int rootBone = skeleton.rootIndex();
        if (rootBone >= 0 && rootBone != hips) {
            out.local[static_cast<std::size_t>(rootBone)] = desired;
        }
        const int parent = skeleton.bone(hips).parent;
        const Transform parentWorld =
            parent >= 0
                ? Transform::fromMatrix(
                      computeBoneWorldMatrix(skeleton, out, parent))
                : Transform::identity();
        hipsTransform =
            Transform::compose(inverseRigid(parentWorld), adjustedWorld);
        return;
    }

    Transform localRoot = hipsTransform;

    // Unity's HumanPose RootT/RootQ are the root (Hips) local transform, not
    // deltas from the rest pose. RetargetTo applies arX after scaling the
    // root translation; write the combined local transform here.
    if (adjusted.hasRootT) {
        localRoot.translation = adjusted.rootT * humanScale;
    }

    if (adjusted.hasRootQ) {
        localRoot.rotation = safeNormalize(adjusted.rootQ);
    }
    hipsTransform = Transform::compose(rootTransform, localRoot);
}

HumanoidSample sampleHumanoidClip(const HumanoidClip& clip, float time) {
    HumanoidSample sample;

    const int count = std::min(static_cast<int>(clip.muscles.size()), kMuscleCount);
    for (int i = 0; i < count; ++i) {
        sample.muscles[i] = evaluateOr(clip.muscles[static_cast<std::size_t>(i)], time, 0.0f);
    }

    if (clip.hasRootT) {
        sample.hasRootT = true;
        for (int i = 0; i < 3; ++i) sample.rootT[i] = evaluateOr(clip.rootT[i], time, 0.0f);
    }

    if (clip.hasRootQ) {
        sample.hasRootQ = true;
        sample.rootQ = quatFromXYZW(evaluateOr(clip.rootQ[0], time, 0.0f),
                                    evaluateOr(clip.rootQ[1], time, 0.0f),
                                    evaluateOr(clip.rootQ[2], time, 0.0f),
                                    evaluateOr(clip.rootQ[3], time, 1.0f));
        sample.rootQ = safeNormalize(sample.rootQ);
    }

    if (clip.hasFootT) {
        sample.hasFootT = true;
        for (int i = 0; i < 3; ++i) {
            sample.leftFootT[i] = evaluateOr(clip.leftFootT[i], time, 0.0f);
            sample.rightFootT[i] = evaluateOr(clip.rightFootT[i], time, 0.0f);
        }
    }

    if (clip.hasFootQ) {
        sample.hasFootQ = true;
        sample.leftFootQ = safeNormalize(quatFromXYZW(
            evaluateOr(clip.leftFootQ[0], time, 0.0f), evaluateOr(clip.leftFootQ[1], time, 0.0f),
            evaluateOr(clip.leftFootQ[2], time, 0.0f), evaluateOr(clip.leftFootQ[3], time, 1.0f)));
        sample.rightFootQ = safeNormalize(quatFromXYZW(
            evaluateOr(clip.rightFootQ[0], time, 0.0f), evaluateOr(clip.rightFootQ[1], time, 0.0f),
            evaluateOr(clip.rightFootQ[2], time, 0.0f), evaluateOr(clip.rightFootQ[3], time, 1.0f)));
    }

    if (clip.hasHandT) {
        sample.hasHandT = true;
        for (int i = 0; i < 3; ++i) {
            sample.leftHandT[i] = evaluateOr(clip.leftHandT[i], time, 0.0f);
            sample.rightHandT[i] = evaluateOr(clip.rightHandT[i], time, 0.0f);
        }
    }

    if (clip.hasHandQ) {
        sample.hasHandQ = true;
        sample.leftHandQ = safeNormalize(quatFromXYZW(
            evaluateOr(clip.leftHandQ[0], time, 0.0f), evaluateOr(clip.leftHandQ[1], time, 0.0f),
            evaluateOr(clip.leftHandQ[2], time, 0.0f), evaluateOr(clip.leftHandQ[3], time, 1.0f)));
        sample.rightHandQ = safeNormalize(quatFromXYZW(
            evaluateOr(clip.rightHandQ[0], time, 0.0f), evaluateOr(clip.rightHandQ[1], time, 0.0f),
            evaluateOr(clip.rightHandQ[2], time, 0.0f), evaluateOr(clip.rightHandQ[3], time, 1.0f)));
    }

    return sample;
}

}  // namespace anim
