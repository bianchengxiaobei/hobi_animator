#include "anim/clip/Clip.h"

#include "anim/avatar/HumanoidAvatar.h"
#include "anim/avatar/MuscleSpace.h"
#include "anim/core/Assert.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {
namespace {

float evaluateOr(const Curve& curve, float time, float fallback) {
    return curve.empty() ? fallback : curve.evaluate(time);
}

}  // namespace

void sampleClip(const Clip& clip, float time, const Skeleton& skeleton,
                const HumanoidAvatar* avatar, Pose& out) {
    ANIM_CHECK_MSG(out.local.size() == skeleton.bones.size(), "pose does not match skeleton");

    // 未出现在 Clip 中的骨骼保持绑定姿态。
    out.resetToRest(skeleton);

    switch (clip.type) {
        case ClipType::BoneLocal: {
            const auto& boneClip = static_cast<const BoneLocalClip&>(clip);

            for (const BoneChannel& channel : boneClip.channels) {
                if (channel.boneIndex < 0) continue;
                ANIM_CHECK_MSG(channel.boneIndex < skeleton.boneCount(),
                               "channel bone index out of range");

                Transform& transform = out.local[static_cast<std::size_t>(channel.boneIndex)];

                if (channel.hasPosition) {
                    transform.translation = Vec3(
                        evaluateOr(channel.position[0], time, transform.translation.x),
                        evaluateOr(channel.position[1], time, transform.translation.y),
                        evaluateOr(channel.position[2], time, transform.translation.z));
                }

                if (channel.hasRotation) {
                    const Quat rotation = quatFromXYZW(
                        evaluateOr(channel.rotation[0], time, transform.rotation.x),
                        evaluateOr(channel.rotation[1], time, transform.rotation.y),
                        evaluateOr(channel.rotation[2], time, transform.rotation.z),
                        evaluateOr(channel.rotation[3], time, transform.rotation.w));
                    transform.rotation = safeNormalize(rotation, transform.rotation);
                }

                if (channel.hasScale) {
                    transform.scale = Vec3(
                        evaluateOr(channel.scale[0], time, transform.scale.x),
                        evaluateOr(channel.scale[1], time, transform.scale.y),
                        evaluateOr(channel.scale[2], time, transform.scale.z));
                }
            }
            return;
        }

        case ClipType::HumanoidMuscle: {
            ANIM_CHECK_MSG(avatar != nullptr, "HumanoidClip requires a HumanoidAvatar");

            const auto& humanoid = static_cast<const HumanoidClip&>(clip);
            const HumanoidSample sample = sampleHumanoidClip(humanoid, time);
            humanoidToLocal(sample, *avatar, skeleton, out);
            return;
        }
    }

    ANIM_CHECK_MSG(false, "unknown clip type");
}

}  // namespace anim
