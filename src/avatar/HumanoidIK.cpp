#include "anim/avatar/HumanoidIK.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

#include "anim/avatar/HumanoidAvatar.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"
#include "../jobs/JobUtils.h"

namespace anim {
namespace {

struct GoalChain {
    HumanBone top;
    HumanBone mid;
    HumanBone low;
    Vec3 position;
    Quat rotation;
    bool hasPosition;
    bool hasRotation;
    float positionWeight;
    float rotationWeight;
    float stretch;
};

Quat nlerpShortest(const Quat& a, const Quat& b, float weight) {
    Quat target = b;
    if (glm::dot(a, target) < 0.0f) target = -target;
    return safeNormalize(a + (target - a) * weight);
}

Quat weightedRotation(const Quat& q, float weight) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    return safeNormalize(Quat(q.w, q.x * weight, q.y * weight, q.z * weight));
}

// Port of math::quatClamp.
Quat clampRotationAngle(const Quat& q, float maxAngle) {
    Quat normalized = safeNormalize(q);
    if (normalized.w < 0.0f) normalized = -normalized;

    const float halfCosMaxAngle = std::cos(0.5f * maxAngle);
    if (normalized.w >= halfCosMaxAngle) return normalized;

    const float fact = (halfCosMaxAngle - normalized.w) / halfCosMaxAngle;
    Quat result = normalized * (1.0f - fact);
    result.w = halfCosMaxAngle + (1.0f - halfCosMaxAngle) * fact;
    return safeNormalize(result);
}

Quat lookAtDeltaQ(const Vec3& pivot, const Vec3& eyesPosition,
                  const Quat& eyesRotation, const Vec3& eyesDirection,
                  const Vec3& target, float weight) {
    const float distance = glm::distance(target, eyesPosition);
    const Vec3 destination = target - pivot;
    const Vec3 source =
        eyesPosition + eyesRotation * (eyesDirection * distance) - pivot;
    return weightedRotation(detail::fromToRotation(source, destination), weight);
}

Vec3 lookAtEyesPosition(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                        const Pose& pose, int head) {
    const int leftEye = avatar.boneIndex(HumanBone::LeftEye);
    const int rightEye = avatar.boneIndex(HumanBone::RightEye);
    if (leftEye >= 0 && rightEye >= 0) {
        // FullBodySolve uses the rest eye translations transformed by the
        // current head, not the current eye world positions.
        const Vec3 restEyes =
            (avatar.rest(HumanBone::LeftEye).translation +
             avatar.rest(HumanBone::RightEye).translation) *
            0.5f;
        const Transform headTransform =
            detail::worldTransform(skeleton, pose, head);
        return headTransform.translation + headTransform.rotation * restEyes;
    }
    return detail::worldPosition(skeleton, pose, head);
}

// Port of mecanim::human::AddAxis: the look-at stages use the skeleton-space
// rotation with the bone's PostQ axis applied.
Quat addAxis(const HumanoidBoneAxes& axes, const Quat& rotation) {
    return axes.valid ? safeNormalize(rotation * axes.postQ) : rotation;
}

Quat removeAxis(const HumanoidBoneAxes& axes, const Quat& rotation) {
    return axes.valid ? safeNormalize(rotation * glm::conjugate(axes.postQ))
                      : rotation;
}

Quat lookAtRotation(const HumanoidAvatar& avatar, HumanBone bone,
                    const Quat& worldRotation) {
    const HumanoidBoneAxes& axes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(bone))];
    return addAxis(axes, worldRotation);
}

Vec3 lookAtForward(const HumanoidAvatar& avatar, HumanBone bone) {
    const HumanoidBoneAxes& axes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(bone))];
    if (!axes.valid) {
        return bone == HumanBone::Head ? Vec3(0.0f, 1.0f, 0.0f)
                                       : Vec3(1.0f, 0.0f, 0.0f);
    }
    return bone == HumanBone::Head ? Vec3(0.0f, -1.0f, 0.0f)
                                   : Vec3(1.0f, 0.0f, 0.0f);
}

void rotateWorld(const Skeleton& skeleton, Pose& pose, int bone,
                 const Quat& delta) {
    const Quat world = detail::worldRotation(skeleton, pose, bone);
    detail::setWorldRotation(pose, skeleton, bone, safeNormalize(delta * world));
}

float limitProject(float minValue, float maxValue, float value) {
    if (value < 0.0f) {
        return minValue < 0.0f ? -value / minValue : (minValue > 0.0f ? value : 0.0f);
    }
    return maxValue > 0.0f ? value / maxValue : (maxValue < 0.0f ? value : 0.0f);
}

float limitUnproject(float minValue, float maxValue, float value) {
    if (value < 0.0f) {
        return minValue < 0.0f ? -value * minValue : (minValue > 0.0f ? value : 0.0f);
    }
    return maxValue > 0.0f ? value * maxValue : (maxValue < 0.0f ? value : 0.0f);
}

Vec3 quat2Qtan(const Quat& q) {
    const float w = std::abs(q.w) > kEpsilon ? q.w : kEpsilon;
    return Vec3(q.x / w, q.y / w, q.z / w);
}

Quat qtan2Quat(const Vec3& qtan) {
    return safeNormalize(Quat(1.0f, qtan.x, qtan.y, qtan.z));
}

Vec3 quat2ZYRoll(const Quat& q) {
    const Vec3 qtan = quat2Qtan(q);
    const float x2p1 = 1.0f + qtan.x * qtan.x;
    return Vec3(qtan.x, (qtan.y - qtan.x * qtan.z) / x2p1,
                (qtan.z + qtan.x * qtan.y) / x2p1);
}

Quat zyRoll2Quat(const Vec3& roll) {
    return safeNormalize(
        Quat(1.0f, roll.x, roll.y + roll.x * roll.z, roll.z - roll.x * roll.y));
}

Vec3 quat2RollZY(const Quat& q) {
    const Vec3 qtan = quat2Qtan(q);
    const float x2p1 = 1.0f + qtan.x * qtan.x;
    return Vec3(qtan.x, (qtan.y + qtan.x * qtan.z) / x2p1,
                (qtan.z - qtan.x * qtan.y) / x2p1);
}

Quat rollZY2Quat(const Vec3& roll) {
    return safeNormalize(
        Quat(1.0f, roll.x, roll.y - roll.x * roll.z, roll.z + roll.x * roll.y));
}

float halfTangent(float radians) {
    constexpr float limit = kPi * 0.5f - 1e-6f;
    return std::tan(std::clamp(radians * 0.5f, -limit, limit));
}

Vec3 toAxes(const HumanoidBoneAxes& axes, const Quat& localRotation) {
    const Quat projected = safeNormalize(
        glm::conjugate(axes.preQ) * localRotation * axes.postQ);
    Vec3 unwrapped;
    if (axes.type == 3) {
        unwrapped = detail::quatToEulerXYZ(projected);
    } else if (axes.type == 1) {
        const Vec3 roll = quat2ZYRoll(projected) * axes.sign;
        unwrapped = Vec3(2.0f * std::atan(roll.x), 2.0f * std::atan(roll.y),
                         2.0f * std::atan(roll.z));
    } else if (axes.type == 2) {
        const Vec3 roll = quat2RollZY(projected) * axes.sign;
        unwrapped = Vec3(2.0f * std::atan(roll.x), 2.0f * std::atan(roll.y),
                         2.0f * std::atan(roll.z));
    } else {
        const Vec3 qtan = quat2Qtan(projected) * axes.sign;
        unwrapped = Vec3(2.0f * std::atan(qtan.x), 2.0f * std::atan(qtan.y),
                         2.0f * std::atan(qtan.z));
    }
    return Vec3(limitProject(axes.limitMin.x, axes.limitMax.x, unwrapped.x),
                limitProject(axes.limitMin.y, axes.limitMax.y, unwrapped.y),
                limitProject(axes.limitMin.z, axes.limitMax.z, unwrapped.z));
}

Quat fromAxes(const HumanoidBoneAxes& axes, const Vec3& uvw) {
    const Vec3 unprojected(
        limitUnproject(axes.limitMin.x, axes.limitMax.x, uvw.x),
        limitUnproject(axes.limitMin.y, axes.limitMax.y, uvw.y),
        limitUnproject(axes.limitMin.z, axes.limitMax.z, uvw.z));
    Quat result;
    if (axes.type == 3) {
        result = detail::eulerXYZToQuat(unprojected);
    } else {
        const Vec3 tangent(halfTangent(unprojected.x * axes.sign.x),
                           halfTangent(unprojected.y * axes.sign.y),
                           halfTangent(unprojected.z * axes.sign.z));
        if (axes.type == 1) {
            result = zyRoll2Quat(tangent);
        } else if (axes.type == 2) {
            result = rollZY2Quat(tangent);
        } else {
            result = qtan2Quat(tangent);
        }
    }
    return safeNormalize(axes.preQ * result * glm::conjugate(axes.postQ));
}

Quat axesProject(const HumanoidBoneAxes& axes, const Quat& rotation) {
    return safeNormalize(glm::conjugate(axes.preQ) * rotation * axes.postQ);
}

Quat axesUnproject(const HumanoidBoneAxes& axes, const Quat& rotation) {
    return safeNormalize(axes.preQ * rotation * glm::conjugate(axes.postQ));
}

// Port of mecanim::skeleton::SkeletonAlign for a valid Axes snapshot.
Quat alignBoneAxes(const HumanoidBoneAxes& axes, const Quat& reference,
                   const Quat& rotation) {
    const Quat refBasis = safeNormalize(reference * axes.postQ);
    const Quat basis = safeNormalize(rotation * axes.postQ);
    const Vec3 refDirection = refBasis * Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 direction = basis * Vec3(1.0f, 0.0f, 0.0f);
    return safeNormalize(detail::fromToRotation(direction, refDirection) * rotation);
}

// Port of mecanim::skeleton::Skeleton2BoneIK's two-bone geometry. When the
// avatar carries source Axes, ToAxes/FromAxes reproduce SkeletonGetDoF and
// SkeletonSetDoF; otherwise both use the quat2Qtan/qtan2Quat fallback. The
// geometric solve stays in the source's half-angle + quatLerp form.
void solveTwoBoneIK(const Skeleton& skeleton, Pose& pose, int top, int mid,
                    int low, const Vec3& target, float weight,
                    const HumanoidBoneAxes* midAxes) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    if (weight <= 0.0f) return;

    const Quat topRest = pose.local[static_cast<std::size_t>(top)].rotation;
    const Quat midOriginal = pose.local[static_cast<std::size_t>(mid)].rotation;

    const Quat topWorld = detail::worldRotation(skeleton, pose, top);
    const Vec3 topPosition = detail::worldPosition(skeleton, pose, top);

    // Skeleton2BoneIK applies SkeletonSetDoF(mid, ToAxes(q) * (1,0,0.9,0))
    // before measuring the chain. Without Axes, Unity uses qtan2Quat here.
    {
        Quat adjusted;
        if (midAxes != nullptr && midAxes->valid) {
            Vec3 dof = toAxes(*midAxes, midOriginal);
            dof.y = 0.0f;
            dof.z *= 0.9f;
            adjusted = fromAxes(*midAxes, dof);
        } else {
            const float w = std::abs(midOriginal.w) > kEpsilon
                                ? midOriginal.w
                                : kEpsilon;
            adjusted = Quat(1.0f, midOriginal.x / w, 0.0f,
                            midOriginal.z / w * 0.9f);
        }
        pose.local[static_cast<std::size_t>(mid)].rotation =
            safeNormalize(adjusted);
    }

    const Quat midWorld = detail::worldRotation(skeleton, pose, mid);
    const Vec3 midPosition = detail::worldPosition(skeleton, pose, mid);
    const Vec3 lowPosition = detail::worldPosition(skeleton, pose, low);
    const Vec3 ab = midPosition - topPosition;
    const Vec3 bc = lowPosition - midPosition;
    const Vec3 ac = lowPosition - topPosition;
    const Vec3 ad = target - topPosition;
    const float lengthAB = glm::length(ab);
    const float lengthBC = glm::length(bc);
    const float lengthAC = glm::length(ac);
    const float lengthAD = glm::length(ad);
    const Vec3 axis = glm::cross(ab, bc);

    Quat elbow = kIdentityQuat;
    if (glm::dot(axis, axis) > kEpsilon) {
        const float angleAC = detail::triangleAngle(lengthAC, ab, bc);
        const float angleAD = detail::triangleAngle(lengthAD, ab, bc);
        const float halfAngle = 0.5f * (angleAC - angleAD);

        // Unity stores q = (axis * sin(a), cos(a)); glm's constructor is
        // (w, x, y, z), so the source half-angle components map explicitly.
        const Vec3 axisNormalized = glm::normalize(axis);
        const float sine = std::sin(halfAngle);
        elbow = Quat(std::cos(halfAngle), sine * axisNormalized.x,
                     sine * axisNormalized.y, sine * axisNormalized.z);
    }
    detail::setWorldRotation(pose, skeleton, mid, elbow * midWorld);

    const Quat solvedMid = pose.local[static_cast<std::size_t>(mid)].rotation;
    pose.local[static_cast<std::size_t>(mid)].rotation =
        nlerpShortest(midOriginal, solvedMid, weight);

    const Vec3 solvedLow = detail::worldPosition(skeleton, pose, low);
    const Vec3 solvedAC = solvedLow - topPosition;
    const Quat shoulderRotation = detail::fromToRotation(solvedAC, ad);
    detail::setWorldRotation(pose, skeleton, top, shoulderRotation * topWorld);

    const Quat solvedTop = pose.local[static_cast<std::size_t>(top)].rotation;
    pose.local[static_cast<std::size_t>(top)].rotation =
        nlerpShortest(solvedTop, topRest, std::pow(1.0f - weight, 4.0f));
}

// Port of mecanim::skeleton::Skeleton2BoneAdjustLength: stretch the two
// segment translations only when the goal lies beyond the current reach.
void adjustChainLength(const Skeleton& skeleton, Pose& pose, int top, int mid,
                       int low, const Vec3& target, float stretch) {
    if (stretch <= 0.0f) return;

    const Vec3 topPosition = detail::worldPosition(skeleton, pose, top);
    const Vec3 midPosition = detail::worldPosition(skeleton, pose, mid);
    const Vec3 lowPosition = detail::worldPosition(skeleton, pose, low);

    const float reach = glm::distance(topPosition, midPosition) +
                        glm::distance(midPosition, lowPosition);
    const float targetDistance = glm::distance(topPosition, target);
    if (!(reach > kEpsilon)) return;

    float ratio = targetDistance / reach;
    const float invStretch = 1.0f - stretch;
    if (!(ratio > invStretch)) return;

    ratio = std::clamp((ratio - invStretch) / (2.0f * stretch), 0.0f, 1.0f);
    const float scale = 1.0f + stretch * ratio * ratio;
    pose.local[static_cast<std::size_t>(mid)].translation *= scale;
    pose.local[static_cast<std::size_t>(low)].translation *= scale;
}

// Port of mecanim::human::HumanFixEndDoF. The child keeps its current global
// orientation while the parent and child local Axes rotations are rebuilt.
void fixEndDoF(const Skeleton& skeleton, Pose& pose, int parent, int child,
               const HumanoidBoneAxes& parentAxes,
               const HumanoidBoneAxes& childAxes) {
    if (parent < 0 || child < 0 || parent == child) return;
    if (!parentAxes.valid || !childAxes.valid) return;

    const int parentParent = skeleton.bone(parent).parent;
    const Quat parentParentWorld =
        parentParent >= 0 ? detail::worldRotation(skeleton, pose, parentParent)
                          : kIdentityQuat;
    const Quat parentLocal = pose.local[static_cast<std::size_t>(parent)].rotation;
    const Quat childWorld = detail::worldRotation(skeleton, pose, child);
    const Quat childRelative = glm::conjugate(parentParentWorld) * childWorld;

    const Quat parentZero = fromAxes(parentAxes, Vec3(0.0f));
    const Quat childZero = fromAxes(childAxes, Vec3(0.0f));
    pose.local[static_cast<std::size_t>(parent)].rotation = parentZero;
    pose.local[static_cast<std::size_t>(child)].rotation = childZero;

    const Quat childZeroRelative =
        glm::conjugate(parentParentWorld) *
        detail::worldRotation(skeleton, pose, child);
    const Quat difference =
        safeNormalize(childRelative * glm::conjugate(childZeroRelative));
    const Quat compensated = safeNormalize(difference * parentZero);
    pose.local[static_cast<std::size_t>(parent)].rotation =
        alignBoneAxes(parentAxes, parentLocal, compensated);
    detail::setWorldRotation(pose, skeleton, child, childWorld);
}

// Port of mecanim::human::HumanFixMidDoF. The child's projected ZYRoll Y
// component is cleared, then the parent compensates so the child's global
// orientation is preserved.
void fixMidDoF(const Skeleton& skeleton, Pose& pose, int parent, int child,
               const HumanoidBoneAxes& parentAxes,
               const HumanoidBoneAxes& childAxes) {
    if (parent < 0 || child < 0 || parent == child) return;
    if (!parentAxes.valid || !childAxes.valid) return;

    const int parentParent = skeleton.bone(parent).parent;
    const Quat parentParentWorld =
        parentParent >= 0 ? detail::worldRotation(skeleton, pose, parentParent)
                          : kIdentityQuat;
    const Quat parentLocal = pose.local[static_cast<std::size_t>(parent)].rotation;
    const Quat childWorld = detail::worldRotation(skeleton, pose, child);
    const Quat childRelative = glm::conjugate(parentParentWorld) * childWorld;

    const Quat projected =
        axesProject(childAxes,
                    pose.local[static_cast<std::size_t>(child)].rotation);
    Vec3 roll = quat2ZYRoll(projected);
    roll.y = 0.0f;
    pose.local[static_cast<std::size_t>(child)].rotation =
        axesUnproject(childAxes, zyRoll2Quat(roll));

    const Quat childNewRelative =
        glm::conjugate(parentParentWorld) *
        detail::worldRotation(skeleton, pose, child);
    const Quat difference =
        safeNormalize(childRelative * glm::conjugate(childNewRelative));
    const Quat compensated = safeNormalize(difference * parentLocal);
    pose.local[static_cast<std::size_t>(parent)].rotation =
        alignBoneAxes(parentAxes, parentLocal, compensated);
    detail::setWorldRotation(pose, skeleton, child, childWorld);
}

void applyChainPosition(const GoalChain& chain, const HumanoidAvatar& avatar,
                        const Skeleton& skeleton, Pose& pose,
                        const Transform& root, float humanScale) {
    if (!chain.hasPosition) return;

    const int top = avatar.boneIndex(chain.top);
    const int mid = avatar.boneIndex(chain.mid);
    const int low = avatar.boneIndex(chain.low);
    if (top < 0 || mid < 0 || low < 0) return;

    const float positionWeight = std::clamp(chain.positionWeight, 0.0f, 1.0f);
    if (positionWeight <= 0.0f) return;

    const Transform current = detail::worldTransform(skeleton, pose, low);
    const HumanoidBoneAxes& lowAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(chain.low))];
    Vec3 goalPosition =
        root.translation + root.rotation * (chain.position * humanScale);
    if (lowAxes.valid &&
        (chain.low == HumanBone::LeftFoot || chain.low == HumanBone::RightFoot)) {
        // RetargetTo subtracts the foot-bottom offset before FullBodySolve.
        const Quat goalRootRotation = safeNormalize(chain.rotation);
        goalPosition -=
            (root.rotation * goalRootRotation) * Vec3(lowAxes.length, 0.0f, 0.0f);
    }

    const Vec3 targetPosition =
        glm::mix(current.translation, goalPosition, positionWeight);
    adjustChainLength(skeleton, pose, top, mid, low, targetPosition,
                      chain.stretch * positionWeight);
    const HumanoidBoneAxes& midAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(chain.mid))];
    solveTwoBoneIK(skeleton, pose, top, mid, low, goalPosition,
                   positionWeight, midAxes.valid ? &midAxes : nullptr);
}

void reachGoalRotation(const Skeleton& skeleton, Pose& pose, int mid, int low,
                       const Quat& endWorldRotation,
                       const HumanoidBoneAxes& midAxes,
                       const HumanoidBoneAxes& lowAxes) {
    const int parent = skeleton.bone(low).parent;
    const Quat parentWorld =
        parent >= 0 ? detail::worldRotation(skeleton, pose, parent)
                    : kIdentityQuat;
    pose.local[static_cast<std::size_t>(low)].rotation =
        safeNormalize(glm::conjugate(parentWorld) * endWorldRotation);
    if (midAxes.valid && lowAxes.valid) {
        fixEndDoF(skeleton, pose, mid, low, midAxes, lowAxes);
    }
}

void applyChainRotation(const GoalChain& chain, const HumanoidAvatar& avatar,
                        const Skeleton& skeleton, Pose& pose,
                        const Transform& root) {
    if (!chain.hasRotation) return;

    const int mid = avatar.boneIndex(chain.mid);
    const int low = avatar.boneIndex(chain.low);
    if (mid < 0 || low < 0) return;

    const float rotationWeight = std::clamp(chain.rotationWeight, 0.0f, 1.0f);
    if (rotationWeight <= 0.0f) return;

    const Transform current = detail::worldTransform(skeleton, pose, low);
    const HumanoidBoneAxes& lowAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(chain.low))];
    Quat targetRotation = current.rotation;
    if (lowAxes.valid) {
        // ReachGoalRotation blends AddAxis(current) with the raw Unity
        // HumanPose goal Q, then RemoveAxis before writing the local pose.
        const Quat currentRoot =
            safeNormalize(glm::conjugate(root.rotation) * current.rotation);
        const Quat blended =
            nlerpShortest(addAxis(lowAxes, currentRoot),
                          safeNormalize(chain.rotation), rotationWeight);
        targetRotation =
            safeNormalize(root.rotation * removeAxis(lowAxes, blended));
    } else {
        const Quat goalRotation =
            safeNormalize(root.rotation * safeNormalize(chain.rotation));
        targetRotation =
            nlerpShortest(current.rotation, goalRotation, rotationWeight);
    }

    const HumanoidBoneAxes& midAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(chain.mid))];
    reachGoalRotation(skeleton, pose, mid, low, targetRotation, midAxes,
                      lowAxes);
}

// Port of mecanim::human::HumanFixTwist. With Axes it uses ToAxes/FromAxes
// and SkeletonAlign; without them it falls back to the qtan path. The child's
// world rotation is restored in both cases.
void fixTwist(const Skeleton& skeleton, Pose& pose, int parent, int child,
              float twist, const HumanoidBoneAxes* axes) {
    if (parent < 0 || child < 0 || parent == child) return;

    const Quat childWorld = detail::worldRotation(skeleton, pose, child);
    const Quat q = pose.local[static_cast<std::size_t>(parent)].rotation;
    Quat adjusted;
    if (axes != nullptr && axes->valid) {
        Vec3 dof = toAxes(*axes, q);
        dof.x *= twist;
        adjusted = alignBoneAxes(*axes, q, fromAxes(*axes, dof));
    } else {
        const float w = std::abs(q.w) > kEpsilon ? q.w : kEpsilon;
        adjusted = Quat(1.0f, q.x / w * twist, q.y / w, q.z / w);
    }
    pose.local[static_cast<std::size_t>(parent)].rotation =
        safeNormalize(adjusted);
    detail::setWorldRotation(pose, skeleton, child, childWorld);
}

void resetUnmappedBones(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                        Pose& pose) {
    std::vector<bool> mapped(static_cast<std::size_t>(skeleton.boneCount()), false);
    for (int i = 0; i < kHumanBoneCount; ++i) {
        const int bone = avatar.boneIndex(static_cast<HumanBone>(i));
        if (bone >= 0 && bone < skeleton.boneCount()) {
            mapped[static_cast<std::size_t>(bone)] = true;
        }
    }

    for (int bone = 0; bone < skeleton.boneCount(); ++bone) {
        if (mapped[static_cast<std::size_t>(bone)]) continue;
        pose.local[static_cast<std::size_t>(bone)].rotation =
            skeleton.bone(bone).restLocal.rotation;
    }
}

void transferMuscle(HumanoidSample& sample, const char* fromName, const char* toName,
                    float ratio) {
    const int from = findMuscleId(fromName);
    const int to = findMuscleId(toName);
    if (from < 0 || to < 0) return;
    sample.muscles[to] += sample.muscles[from] * ratio;
    sample.muscles[from] = 0.0f;
}

// Port of HumanPoseAdjustForMissingBones for the muscles represented by this
// library's 95-value table.
void adjustMissingBones(const HumanoidAvatar& avatar, HumanoidSample& sample) {
    if (!avatar.hasBone(HumanBone::Neck)) {
        transferMuscle(sample, "Neck Nod Down-Up", "Head Nod Down-Up", 1.0f);
        transferMuscle(sample, "Neck Tilt Left-Right", "Head Tilt Left-Right",
                       1.0f);
        transferMuscle(sample, "Neck Turn Left-Right", "Head Turn Left-Right",
                       1.0f);
    }

    if (!avatar.hasBone(HumanBone::Chest)) {
        transferMuscle(sample, "Chest Front-Back", "Spine Front-Back", 1.0f);
        transferMuscle(sample, "Chest Left-Right", "Spine Left-Right", 1.0f);
        transferMuscle(sample, "Chest Twist Left-Right", "Spine Twist Left-Right",
                       1.0f);
    }

    if (!avatar.hasBone(HumanBone::LeftShoulder)) {
        transferMuscle(sample, "Left Shoulder Down-Up", "Left Arm Down-Up",
                       30.0f / 200.0f);
        transferMuscle(sample, "Left Shoulder Front-Back", "Left Arm Front-Back",
                       45.0f / 160.0f);
    }

    if (!avatar.hasBone(HumanBone::RightShoulder)) {
        transferMuscle(sample, "Right Shoulder Down-Up", "Right Arm Down-Up",
                       30.0f / 200.0f);
        transferMuscle(sample, "Right Shoulder Front-Back", "Right Arm Front-Back",
                       45.0f / 160.0f);
    }
}

void extractGoalsFromReference(const HumanoidAvatar& avatar,
                               const Skeleton& skeleton, const Pose& rootPose,
                               const Pose& referencePose, float scale,
                               HumanoidSample& sample) {
    const int hips = avatar.boneIndex(HumanBone::Hips);
    sample.hasFootT = false;
    sample.hasFootQ = false;
    sample.hasHandT = false;
    sample.hasHandQ = false;
    if (hips < 0) return;

    if (!(std::abs(scale) > kEpsilon)) scale = 1.0f;
    Transform root = detail::worldTransform(skeleton, rootPose, hips);
    if (sample.rootMode == HumanoidRootMode::MassCenter) {
        const Quat reference =
            detail::humanoidMassCenterReferenceRotation(avatar, skeleton);
        root = detail::humanoidMassCenterRoot(avatar, skeleton, rootPose,
                                              reference);
    }

    struct GoalInfo {
        HumanBone end;
        bool foot;
    };
    const GoalInfo goals[] = {
        {HumanBone::LeftFoot, true},
        {HumanBone::RightFoot, true},
        {HumanBone::LeftHand, false},
        {HumanBone::RightHand, false},
    };

    for (const GoalInfo& goal : goals) {
        const int end = avatar.boneIndex(goal.end);
        if (end < 0) continue;

        const Transform endTransform =
            detail::worldTransform(skeleton, referencePose, end);
        const HumanoidBoneAxes& axes =
            avatar.axes[static_cast<std::size_t>(humanBoneIndex(goal.end))];
        const Quat goalAxesRotation = addAxis(axes, endTransform.rotation);
        Vec3 goalWorld = endTransform.translation;
        if (goal.foot && axes.valid) {
            goalWorld += goalAxesRotation * Vec3(axes.length, 0.0f, 0.0f);
        }

        const Vec3 goalRoot =
            glm::conjugate(root.rotation) *
            (goalWorld - root.translation) / scale;
        const Quat goalRotation =
            safeNormalize(glm::conjugate(root.rotation) * goalAxesRotation);
        if (goal.foot) {
            sample.hasFootT = true;
            sample.hasFootQ = true;
            if (goal.end == HumanBone::LeftFoot) {
                sample.leftFootT = goalRoot;
                sample.leftFootQ = goalRotation;
            } else {
                sample.rightFootT = goalRoot;
                sample.rightFootQ = goalRotation;
            }
        } else {
            sample.hasHandT = true;
            sample.hasHandQ = true;
            if (goal.end == HumanBone::LeftHand) {
                sample.leftHandT = goalRoot;
                sample.leftHandQ = goalRotation;
            } else {
                sample.rightHandT = goalRoot;
                sample.rightHandQ = goalRotation;
            }
        }
    }
}

float humanBoneDefaultMass(HumanBone bone) {
    switch (bone) {
        case HumanBone::Hips: return 12.0f;
        case HumanBone::LeftUpperLeg: return 10.0f;
        case HumanBone::RightUpperLeg: return 10.0f;
        case HumanBone::LeftLowerLeg: return 4.0f;
        case HumanBone::RightLowerLeg: return 4.0f;
        case HumanBone::LeftFoot: return 0.8f;
        case HumanBone::RightFoot: return 0.8f;
        case HumanBone::Spine: return 2.5f;
        case HumanBone::Chest: return 24.0f;
        case HumanBone::Neck: return 1.0f;
        case HumanBone::Head: return 4.0f;
        case HumanBone::LeftShoulder: return 0.5f;
        case HumanBone::RightShoulder: return 0.5f;
        case HumanBone::LeftUpperArm: return 2.0f;
        case HumanBone::RightUpperArm: return 2.0f;
        case HumanBone::LeftLowerArm: return 1.5f;
        case HumanBone::RightLowerArm: return 1.5f;
        case HumanBone::LeftHand: return 0.5f;
        case HumanBone::RightHand: return 0.5f;
        case HumanBone::LeftToes: return 0.2f;
        case HumanBone::RightToes: return 0.2f;
        default: return 0.0f;
    }
}

Quat quatFromBasis(const Vec3& u, const Vec3& v, const Vec3& w) {
    Quat q;
    if (u.x >= 0.0f) {
        const float t = v.y + w.z;
        if (t >= 0.0f) {
            q = Quat(1.0f + u.x + t, v.z - w.y, w.x - u.z, u.y - v.x);
        } else {
            q = Quat(v.z - w.y, 1.0f + u.x - t, u.y + v.x, w.x + u.z);
        }
    } else {
        const float t = v.y - w.z;
        if (t >= 0.0f) {
            q = Quat(v.z + w.y, w.x - u.z, u.y + v.x, 1.0f - u.x + t);
        } else {
            q = Quat(v.z + w.y, u.y - v.x, w.x + u.z, 1.0f - u.x - t);
        }
    }
    return safeNormalize(q);
}

}  // namespace

namespace detail {

void adjustHumanoidMissingBones(const HumanoidAvatar& avatar,
                                HumanoidSample& sample) {
    adjustMissingBones(avatar, sample);
}

Vec3 quatToEulerXYZ(const Quat& q) {
    constexpr float epsilon = 1e-6f;
    const float xz = q.x * q.z;
    const float yw = q.y * q.w;
    const float discr = xz - yw;

    if (discr >= 0.5f - epsilon) {
        return Vec3(std::atan2(q.x * q.w - q.y * q.z, -xz - yw),
                    -0.5f * kPi, 0.0f);
    }
    if (discr <= epsilon - 0.5f) {
        return Vec3(std::atan2(q.x * q.y - q.w * q.z,
                               q.y * q.y + q.w * q.w - 0.5f),
                    0.5f * kPi, 0.0f);
    }

    return Vec3(std::atan2(q.x * q.w + q.y * q.z,
                           q.z * q.z + q.w * q.w - 0.5f),
                -std::asin(2.0f * discr),
                std::atan2(q.x * q.y + q.w * q.z,
                           q.x * q.x + q.w * q.w - 0.5f));
}

Quat eulerXYZToQuat(const Vec3& euler) {
    const float sx = std::sin(0.5f * euler.x);
    const float sy = std::sin(0.5f * euler.y);
    const float sz = std::sin(0.5f * euler.z);
    const float cx = std::cos(0.5f * euler.x);
    const float cy = std::cos(0.5f * euler.y);
    const float cz = std::cos(0.5f * euler.z);

    return Quat(cy * cx * cz + sy * sx * sz,
                cy * sx * cz - sy * cx * sz,
                cy * sx * sz + sy * cx * cz,
                cy * cx * sz - sy * sx * cz);
}

Transform humanoidMassCenterRoot(const HumanoidAvatar& avatar,
                                 const Skeleton& skeleton, const Pose& pose,
                                 const Quat& referenceRotation) {
    const int hips = avatar.boneIndex(HumanBone::Hips);
    if (hips < 0) return Transform::identity();
    const Vec3 hipsPosition = worldPosition(skeleton, pose, hips);

    std::array<float, kHumanBoneCount> masses{};
    for (int i = 0; i < kHumanBoneCount; ++i) {
        masses[static_cast<std::size_t>(i)] =
            humanBoneDefaultMass(static_cast<HumanBone>(i));
    }

    const auto transfer = [&](HumanBone from, HumanBone to) {
        if (avatar.hasBone(from)) return;
        masses[static_cast<std::size_t>(humanBoneIndex(to))] +=
            masses[static_cast<std::size_t>(humanBoneIndex(from))];
        masses[static_cast<std::size_t>(humanBoneIndex(from))] = 0.0f;
    };
    transfer(HumanBone::Neck, HumanBone::Chest);
    transfer(HumanBone::LeftShoulder, HumanBone::Chest);
    transfer(HumanBone::RightShoulder, HumanBone::Chest);
    transfer(HumanBone::Chest, HumanBone::Spine);
    transfer(HumanBone::LeftToes, HumanBone::LeftFoot);
    transfer(HumanBone::RightToes, HumanBone::RightFoot);

    const auto worldOr = [&](HumanBone bone, const Vec3& fallback) {
        const int index = avatar.boneIndex(bone);
        return index >= 0 ? worldPosition(skeleton, pose, index) : fallback;
    };
    const auto average = [&](std::initializer_list<HumanBone> bones,
                             const Vec3& fallback) {
        Vec3 sum(0.0f);
        int count = 0;
        for (HumanBone bone : bones) {
            const int index = avatar.boneIndex(bone);
            if (index < 0) continue;
            sum += worldPosition(skeleton, pose, index);
            ++count;
        }
        return count > 0 ? sum / static_cast<float>(count) : fallback;
    };
    const auto center = [&](HumanBone bone) {
        switch (bone) {
            case HumanBone::Hips:
                return average({HumanBone::LeftUpperLeg,
                                HumanBone::RightUpperLeg, HumanBone::Spine},
                               hipsPosition);
            case HumanBone::Spine:
                if (avatar.hasBone(HumanBone::Chest)) {
                    return average({HumanBone::Spine, HumanBone::Chest},
                                   hipsPosition);
                }
                if (avatar.hasBone(HumanBone::LeftUpperArm) &&
                    avatar.hasBone(HumanBone::RightUpperArm)) {
                    return 0.1f * worldOr(HumanBone::Spine, hipsPosition) +
                           0.45f * (worldOr(HumanBone::LeftUpperArm, hipsPosition) +
                                    worldOr(HumanBone::RightUpperArm, hipsPosition));
                }
                return worldOr(HumanBone::Spine, hipsPosition);
            case HumanBone::Chest:
                if (avatar.hasBone(HumanBone::Neck) &&
                    avatar.hasBone(HumanBone::LeftShoulder) &&
                    avatar.hasBone(HumanBone::RightShoulder)) {
                    return average({HumanBone::Chest, HumanBone::Neck,
                                    HumanBone::LeftShoulder,
                                    HumanBone::RightShoulder},
                                   hipsPosition);
                }
                if (avatar.hasBone(HumanBone::LeftUpperArm) &&
                    avatar.hasBone(HumanBone::RightUpperArm)) {
                    return average({HumanBone::Chest, HumanBone::LeftUpperArm,
                                    HumanBone::RightUpperArm},
                                   hipsPosition);
                }
                return worldOr(HumanBone::Chest, hipsPosition);
            case HumanBone::Neck:
                return average({HumanBone::Neck, HumanBone::Head}, hipsPosition);
            case HumanBone::LeftUpperLeg:
                return average({HumanBone::LeftUpperLeg, HumanBone::LeftLowerLeg},
                               hipsPosition);
            case HumanBone::LeftLowerLeg:
                return average({HumanBone::LeftLowerLeg, HumanBone::LeftFoot},
                               hipsPosition);
            case HumanBone::LeftShoulder:
                return average({HumanBone::LeftShoulder, HumanBone::LeftUpperArm},
                               hipsPosition);
            case HumanBone::LeftUpperArm:
                return average({HumanBone::LeftUpperArm, HumanBone::LeftLowerArm},
                               hipsPosition);
            case HumanBone::LeftLowerArm:
                return average({HumanBone::LeftUpperArm, HumanBone::LeftHand},
                               hipsPosition);
            case HumanBone::RightUpperLeg:
                return average({HumanBone::RightUpperLeg, HumanBone::RightLowerLeg},
                               hipsPosition);
            case HumanBone::RightLowerLeg:
                return average({HumanBone::RightLowerLeg, HumanBone::RightFoot},
                               hipsPosition);
            case HumanBone::RightShoulder:
                return average({HumanBone::RightShoulder, HumanBone::RightUpperArm},
                               hipsPosition);
            case HumanBone::RightUpperArm:
                return average({HumanBone::RightUpperArm, HumanBone::RightLowerArm},
                               hipsPosition);
            case HumanBone::RightLowerArm:
                return average({HumanBone::RightUpperArm, HumanBone::RightHand},
                               hipsPosition);
            default:
                return worldOr(bone, hipsPosition);
        }
    };

    Vec3 weighted(0.0f);
    float totalMass = 0.0f;
    for (int i = 0; i < kHumanBoneCount; ++i) {
        const float mass = masses[static_cast<std::size_t>(i)];
        const HumanBone bone = static_cast<HumanBone>(i);
        if (mass <= 0.0f || !avatar.hasBone(bone)) continue;
        weighted += center(bone) * mass;
        totalMass += mass;
    }
    const Vec3 massCenter =
        totalMass > kEpsilon ? weighted / totalMass : hipsPosition;

    Quat orientation = worldRotation(skeleton, pose, hips);
    if (avatar.hasBone(HumanBone::LeftUpperLeg) &&
        avatar.hasBone(HumanBone::RightUpperLeg) &&
        avatar.hasBone(HumanBone::LeftUpperArm) &&
        avatar.hasBone(HumanBone::RightUpperArm)) {
        const Vec3 leftLeg = worldOr(HumanBone::LeftUpperLeg, hipsPosition);
        const Vec3 rightLeg = worldOr(HumanBone::RightUpperLeg, hipsPosition);
        const Vec3 leftArm = worldOr(HumanBone::LeftUpperArm, hipsPosition);
        const Vec3 rightArm = worldOr(HumanBone::RightUpperArm, hipsPosition);
        const Vec3 legCenter = 0.5f * (leftLeg + rightLeg);
        const Vec3 armCenter = 0.5f * (leftArm + rightArm);
        const Vec3 upVector = armCenter - legCenter;
        const Vec3 rightVector =
            (rightLeg - leftLeg) + (rightArm - leftArm);
        if (glm::length(upVector) > kEpsilon &&
            glm::length(rightVector) > kEpsilon) {
            const Vec3 up = glm::normalize(upVector);
            Vec3 right = glm::normalize(rightVector);
            const Vec3 front = glm::cross(right, up);
            right = glm::cross(up, front);
            if (glm::length(front) > kEpsilon &&
                glm::length(right) > kEpsilon) {
                orientation = quatFromBasis(right, up, front);
            }
        }
    }

    orientation = safeNormalize(orientation * glm::conjugate(referenceRotation));
    return Transform(massCenter, orientation, Vec3(1.0f));
}

Transform humanoidMassCenterRoot(const HumanoidAvatar& avatar,
                                 const Skeleton& skeleton, const Pose& pose) {
    return humanoidMassCenterRoot(avatar, skeleton, pose, kIdentityQuat);
}

Quat humanoidMassCenterReferenceRotation(const HumanoidAvatar& avatar,
                                         const Skeleton& skeleton) {
    Pose rest;
    rest.resizeLike(skeleton);
    rest.resetToRest(skeleton);
    return humanoidMassCenterRoot(avatar, skeleton, rest, kIdentityQuat).rotation;
}

}  // namespace detail

// Retarget helper: clear the mid bone's projected twist and let its parent
// absorb the difference while keeping the child's world orientation.
void fixHumanoidMidDoF(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                       Pose& pose, HumanBone parent, HumanBone child) {
    const int parentIndex = avatar.boneIndex(parent);
    const int childIndex = avatar.boneIndex(child);
    if (parentIndex < 0 || childIndex < 0) return;
    const HumanoidBoneAxes& parentAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(parent))];
    const HumanoidBoneAxes& childAxes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(child))];
    fixMidDoF(skeleton, pose, parentIndex, childIndex, parentAxes, childAxes);
}

// Retarget helper: canonicalize the child's Axes limits and align its world
// rotation to the reference pose, then write the local rotation back.
void alignHumanoidBoneToReference(const HumanoidAvatar& avatar,
                                  const Skeleton& skeleton, Pose& pose,
                                  const Pose& referencePose, HumanBone child,
                                  HumanBone parent) {
    const int childIndex = avatar.boneIndex(child);
    const int parentIndex = avatar.boneIndex(parent);
    if (childIndex < 0 || parentIndex < 0) return;
    const HumanoidBoneAxes& axes =
        avatar.axes[static_cast<std::size_t>(humanBoneIndex(child))];
    if (!axes.valid) return;

    pose.local[static_cast<std::size_t>(childIndex)].rotation =
        fromAxes(axes, toAxes(
                           axes,
                           pose.local[static_cast<std::size_t>(childIndex)].rotation));
    const Quat childWorld = detail::worldRotation(skeleton, pose, childIndex);
    const Quat referenceWorld =
        detail::worldRotation(skeleton, referencePose, childIndex);
    const Quat aligned = alignBoneAxes(axes, referenceWorld, childWorld);
    const Quat parentWorld = detail::worldRotation(skeleton, pose, parentIndex);
    pose.local[static_cast<std::size_t>(childIndex)].rotation =
        safeNormalize(glm::conjugate(parentWorld) * aligned);
}

// RetargetFrom's pose repair stage: multi-round shoulder/limb alignment plus
// mid DoF and endpoint fixes. referencePose supplies the desired world
// orientations on the same skeleton.
void retargetHumanoidPose(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                          Pose& pose, const Pose& referencePose,
                          const HumanoidRetargetSettings& settings) {
    const int iterations = std::max(0, settings.fixIterations);

    auto align = [&](HumanBone child, HumanBone parent) {
        alignHumanoidBoneToReference(avatar, skeleton, pose, referencePose, child,
                                     parent);
    };
    auto fixMid = [&](HumanBone parent, HumanBone child) {
        fixHumanoidMidDoF(avatar, skeleton, pose, parent, child);
    };

    for (int iteration = 0; iteration < iterations; ++iteration) {
        if (avatar.hasBone(HumanBone::LeftShoulder)) {
            align(HumanBone::LeftShoulder, HumanBone::LeftShoulder);
        }
        if (avatar.hasBone(HumanBone::RightShoulder)) {
            align(HumanBone::RightShoulder, HumanBone::RightShoulder);
        }
    }

    for (int iteration = 0; iteration < iterations; ++iteration) {
        align(HumanBone::LeftUpperArm,
              avatar.hasBone(HumanBone::LeftShoulder) ? HumanBone::LeftShoulder
                                                      : HumanBone::LeftUpperArm);
        align(HumanBone::RightUpperArm,
              avatar.hasBone(HumanBone::RightShoulder) ? HumanBone::RightShoulder
                                                       : HumanBone::RightUpperArm);
        align(HumanBone::LeftUpperLeg, HumanBone::LeftUpperLeg);
        align(HumanBone::RightUpperLeg, HumanBone::RightUpperLeg);
    }

    for (int iteration = 0; iteration < iterations; ++iteration) {
        fixMid(HumanBone::LeftUpperArm, HumanBone::LeftLowerArm);
        fixMid(HumanBone::RightUpperArm, HumanBone::RightLowerArm);
        fixMid(HumanBone::LeftUpperLeg, HumanBone::LeftLowerLeg);
        fixMid(HumanBone::RightUpperLeg, HumanBone::RightLowerLeg);

        align(HumanBone::LeftLowerArm, HumanBone::LeftUpperArm);
        align(HumanBone::RightLowerArm, HumanBone::RightUpperArm);
        align(HumanBone::LeftLowerLeg, HumanBone::LeftUpperLeg);
        align(HumanBone::RightLowerLeg, HumanBone::RightUpperLeg);
    }

    const std::pair<HumanBone, HumanBone> endpoints[] = {
        {HumanBone::LeftLowerArm, HumanBone::LeftHand},
        {HumanBone::RightLowerArm, HumanBone::RightHand},
        {HumanBone::LeftLowerLeg, HumanBone::LeftFoot},
        {HumanBone::RightLowerLeg, HumanBone::RightFoot},
    };
    for (const auto& endpoint : endpoints) {
        const int parentIndex = avatar.boneIndex(endpoint.first);
        const int childIndex = avatar.boneIndex(endpoint.second);
        if (parentIndex < 0 || childIndex < 0) continue;
        detail::setWorldRotation(
            pose, skeleton, childIndex,
            detail::worldRotation(skeleton, referencePose, childIndex));
        const HumanoidBoneAxes& parentAxes =
            avatar.axes[static_cast<std::size_t>(
                humanBoneIndex(endpoint.first))];
        const HumanoidBoneAxes& childAxes =
            avatar.axes[static_cast<std::size_t>(
                humanBoneIndex(endpoint.second))];
        if (parentAxes.valid && childAxes.valid) {
            fixEndDoF(skeleton, pose, parentIndex, childIndex, parentAxes,
                      childAxes);
        }
    }
}

HumanoidSample retargetHumanoidFromPose(
    const HumanoidAvatar& avatar, const Skeleton& skeleton, const Pose& pose,
    const HumanoidRetargetSettings& settings, float humanScale) {
    const Pose reference = pose;
    Pose working = pose;
    resetUnmappedBones(avatar, skeleton, working);
    retargetHumanoidPose(avatar, skeleton, working, reference, settings);

    float scale = humanScale;
    if (!(std::abs(scale) > kEpsilon)) {
        scale = avatar.hipsHeight > kEpsilon ? avatar.hipsHeight : 1.0f;
    }
    HumanoidSample sample = humanoidLocalToSample(avatar, skeleton, working, scale,
                                                  settings.rootMode);
    extractGoalsFromReference(avatar, skeleton, working, reference, scale, sample);
    return sample;
}

HumanoidSample humanoidLocalToSample(const HumanoidAvatar& avatar,
                                     const Skeleton& skeleton, const Pose& pose,
                                     float humanScale, HumanoidRootMode rootMode) {
    HumanoidSample sample;
    sample.rootMode = rootMode;

    for (int i = 0; i < kHumanBoneCount; ++i) {
        if (!avatar.hasUvw[static_cast<std::size_t>(i)]) continue;
        const int bone = avatar.boneIndex(static_cast<HumanBone>(i));
        if (bone < 0) continue;

        const HumanBone humanBone = static_cast<HumanBone>(i);
        const Quat restPre =
            safeNormalize(avatar.rest(humanBone).rotation * avatar.pre(humanBone));
        const Quat local =
            pose.local[static_cast<std::size_t>(bone)].rotation;
        const Quat delta = safeNormalize(
            glm::conjugate(restPre) * local *
            glm::conjugate(avatar.post(humanBone)));
        const Vec3 tangent = quat2ZYRoll(delta);
        for (int slot = 0; slot < 3; ++slot) {
            const HumanoidAxisBinding binding =
                avatar.uvw[static_cast<std::size_t>(i)]
                          [static_cast<std::size_t>(slot)];
            if (binding.muscleId < 0) continue;
            const MuscleLimit limit = avatar.limit(binding.muscleId);
            const float minRadians = radians(limit.minDegrees);
            const float maxRadians = radians(limit.maxDegrees);
            const float rawAngle =
                2.0f * std::atan(tangent[slot] * binding.sign);
            float value = 0.0f;
            if (rawAngle >= 0.0f) {
                if (maxRadians > kEpsilon) value = rawAngle / maxRadians;
            } else if (minRadians < -kEpsilon) {
                value = rawAngle / (-minRadians);
            }
            sample.muscles[binding.muscleId] =
                std::clamp(value, -1.0f, 1.0f);
        }
    }

    const float scale = std::abs(humanScale) > kEpsilon ? humanScale : 1.0f;
    const int hips = avatar.boneIndex(HumanBone::Hips);
    if (hips < 0) return sample;

    const Transform hipsRoot = detail::worldTransform(skeleton, pose, hips);
    Transform goalSpace = hipsRoot;
    sample.hasRootT = true;
    sample.hasRootQ = true;
    if (rootMode == HumanoidRootMode::MassCenter) {
        const Quat reference =
            detail::humanoidMassCenterReferenceRotation(avatar, skeleton);
        goalSpace = detail::humanoidMassCenterRoot(avatar, skeleton, pose,
                                                   reference);
        sample.rootT = goalSpace.translation / scale;
        sample.rootQ = safeNormalize(goalSpace.rotation);
    } else {
        sample.rootT =
            pose.local[static_cast<std::size_t>(hips)].translation / scale;
        sample.rootQ = safeNormalize(
            pose.local[static_cast<std::size_t>(hips)].rotation);
    }

    struct GoalInfo {
        HumanBone end;
        bool foot;
    };
    const GoalInfo goals[] = {
        {HumanBone::LeftFoot, true},
        {HumanBone::RightFoot, true},
        {HumanBone::LeftHand, false},
        {HumanBone::RightHand, false},
    };
    for (const GoalInfo& goal : goals) {
        const int end = avatar.boneIndex(goal.end);
        if (end < 0) continue;
        const Transform endTransform =
            detail::worldTransform(skeleton, pose, end);
        const HumanoidBoneAxes& axes =
            avatar.axes[static_cast<std::size_t>(humanBoneIndex(goal.end))];
        const Quat goalAxesRotation = addAxis(axes, endTransform.rotation);
        Vec3 goalWorld = endTransform.translation;
        if (goal.foot && axes.valid) {
            goalWorld += goalAxesRotation * Vec3(axes.length, 0.0f, 0.0f);
        }
        const Vec3 goalRoot =
            glm::conjugate(goalSpace.rotation) *
            (goalWorld - goalSpace.translation) / scale;
        const Quat goalRotation =
            safeNormalize(glm::conjugate(goalSpace.rotation) *
                          goalAxesRotation);
        if (goal.foot) {
            sample.hasFootT = true;
            sample.hasFootQ = true;
            if (goal.end == HumanBone::LeftFoot) {
                sample.leftFootT = goalRoot;
                sample.leftFootQ = goalRotation;
            } else {
                sample.rightFootT = goalRoot;
                sample.rightFootQ = goalRotation;
            }
        } else {
            sample.hasHandT = true;
            sample.hasHandQ = true;
            if (goal.end == HumanBone::LeftHand) {
                sample.leftHandT = goalRoot;
                sample.leftHandQ = goalRotation;
            } else {
                sample.rightHandT = goalRoot;
                sample.rightHandQ = goalRotation;
            }
        }
    }
    return sample;
}

void retargetHumanoidLocalPose(const HumanoidAvatar& sourceAvatar,
                               const Skeleton& sourceSkeleton,
                               const Pose& sourcePose,
                               const HumanoidAvatar& targetAvatar,
                               const Skeleton& targetSkeleton, Pose& targetPose,
                               float humanScale, HumanoidRootMode rootMode) {
    float sourceScale = 1.0f;
    float targetScale = humanScale;

    if (!(targetScale > 0.0f)) {
        sourceScale =
            sourceAvatar.hipsHeight > kEpsilon ? sourceAvatar.hipsHeight : 1.0f;
        targetScale =
            targetAvatar.hipsHeight > kEpsilon ? targetAvatar.hipsHeight : 1.0f;
    }

    HumanoidRetargetSettings settings;
    settings.rootMode = rootMode;
    const HumanoidSample sample =
        retargetHumanoidFromPose(sourceAvatar, sourceSkeleton, sourcePose,
                                 settings, sourceScale);
    humanoidToLocal(sample, targetAvatar, targetSkeleton, targetPose, targetScale);
}

void applyHumanoidGoals(const HumanoidSample& sample, const HumanoidAvatar& avatar,
                        const Skeleton& skeleton, Pose& pose,
                        const HumanoidGoalWeights& weights, float humanScale) {
    const int hips = avatar.boneIndex(HumanBone::Hips);
    if (hips < 0) return;

    Transform root = detail::worldTransform(skeleton, pose, hips);
    if (sample.rootMode == HumanoidRootMode::MassCenter) {
        const Quat reference =
            detail::humanoidMassCenterReferenceRotation(avatar, skeleton);
        root = detail::humanoidMassCenterRoot(avatar, skeleton, pose, reference);
    }

    const GoalChain chains[] = {
        {HumanBone::LeftUpperLeg, HumanBone::LeftLowerLeg, HumanBone::LeftFoot,
         sample.leftFootT, sample.leftFootQ, sample.hasFootT, sample.hasFootQ,
         weights.leftFootPosition, weights.leftFootRotation, weights.legStretch},
        {HumanBone::RightUpperLeg, HumanBone::RightLowerLeg, HumanBone::RightFoot,
         sample.rightFootT, sample.rightFootQ, sample.hasFootT, sample.hasFootQ,
         weights.rightFootPosition, weights.rightFootRotation, weights.legStretch},
        {HumanBone::LeftUpperArm, HumanBone::LeftLowerArm, HumanBone::LeftHand,
         sample.leftHandT, sample.leftHandQ, sample.hasHandT, sample.hasHandQ,
         weights.leftHandPosition, weights.leftHandRotation, weights.armStretch},
        {HumanBone::RightUpperArm, HumanBone::RightLowerArm, HumanBone::RightHand,
         sample.rightHandT, sample.rightHandQ, sample.hasHandT, sample.hasHandQ,
         weights.rightHandPosition, weights.rightHandRotation, weights.armStretch},
    };

    for (const GoalChain& chain : chains) {
        applyChainPosition(chain, avatar, skeleton, pose, root, humanScale);
    }
    for (const GoalChain& chain : chains) {
        applyChainRotation(chain, avatar, skeleton, pose, root);
    }
}

void applyHumanoidTwist(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                        Pose& pose, const HumanoidTwistWeights& weights) {
    struct TwistPair {
        HumanBone parent;
        HumanBone child;
        float weight;
    };
    const TwistPair pairs[] = {
        {HumanBone::LeftLowerArm, HumanBone::LeftHand, weights.foreArmTwist},
        {HumanBone::LeftUpperArm, HumanBone::LeftLowerArm, weights.armTwist},
        {HumanBone::RightLowerArm, HumanBone::RightHand, weights.foreArmTwist},
        {HumanBone::RightUpperArm, HumanBone::RightLowerArm, weights.armTwist},
        {HumanBone::LeftLowerLeg, HumanBone::LeftFoot, weights.legTwist},
        {HumanBone::LeftUpperLeg, HumanBone::LeftLowerLeg, weights.upperLegTwist},
        {HumanBone::RightLowerLeg, HumanBone::RightFoot, weights.legTwist},
        {HumanBone::RightUpperLeg, HumanBone::RightLowerLeg, weights.upperLegTwist},
    };

    for (const TwistPair& pair : pairs) {
        const HumanoidBoneAxes& axes =
            avatar.axes[static_cast<std::size_t>(humanBoneIndex(pair.parent))];
        fixTwist(skeleton, pose, avatar.boneIndex(pair.parent),
                 avatar.boneIndex(pair.child), pair.weight,
                 axes.valid ? &axes : nullptr);
    }
}

void applyHumanoidTwist(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                        Pose& pose) {
    const HumanoidTwistWeights weights{avatar.armTwist, avatar.foreArmTwist,
                                       avatar.upperLegTwist, avatar.legTwist};
    applyHumanoidTwist(avatar, skeleton, pose, weights);
}

void applyHumanoidLookAt(const HumanoidAvatar& avatar, const Skeleton& skeleton,
                         Pose& pose, const Vec3& targetPosition,
                         const HumanoidLookAtWeights& weights) {
    const int head = avatar.boneIndex(HumanBone::Head);
    if (head < 0) return;

    const int spine = avatar.boneIndex(HumanBone::Spine);
    const int chest = avatar.boneIndex(HumanBone::Chest);
    const int neck = avatar.boneIndex(HumanBone::Neck);
    const float clampWeight = std::clamp(weights.clamp, 0.0f, 1.0f);
    const float bodyWeight = std::clamp(weights.body, 0.0f, 1.0f);
    const float headWeight = std::clamp(weights.head, 0.0f, 1.0f);
    const float eyeWeight = std::clamp(weights.eyes, 0.0f, 1.0f);

    Vec3 headGoal = targetPosition;
    const Vec3 headForward = lookAtForward(avatar, HumanBone::Head);

    // FullBodySolve's clamp stage limits the head goal by rotating the
    // current eyes forward vector toward the requested target.
    if (clampWeight > 0.0f) {
        const Vec3 eyesPosition =
            lookAtEyesPosition(avatar, skeleton, pose, head);
        const Quat eyesRotation =
            lookAtRotation(avatar, HumanBone::Head,
                           detail::worldRotation(skeleton, pose, head));
        const Vec3 destination = headGoal - eyesPosition;
        const Vec3 source =
            eyesRotation * (headForward * glm::length(destination));
        const Quat delta = clampRotationAngle(
            detail::fromToRotation(source, destination),
            radians(180.0f * (1.0f - clampWeight)));
        headGoal = eyesPosition + delta * source;
    }

    if (bodyWeight > 0.0f && spine >= 0) {
        const float spineWeight = chest >= 0 ? 0.5f * bodyWeight : bodyWeight;
        Vec3 eyesPosition = detail::worldPosition(skeleton, pose, head);
        Quat eyesRotation =
            lookAtRotation(avatar, HumanBone::Head,
                           detail::worldRotation(skeleton, pose, head));
        const Vec3 spinePosition = detail::worldPosition(skeleton, pose, spine);
        rotateWorld(skeleton, pose, spine,
                    lookAtDeltaQ(spinePosition, eyesPosition, eyesRotation,
                                 headForward, headGoal, spineWeight));

        if (chest >= 0) {
            eyesPosition = lookAtEyesPosition(avatar, skeleton, pose, head);
            eyesRotation = lookAtRotation(avatar, HumanBone::Head,
                                          detail::worldRotation(skeleton, pose, head));
            const Vec3 chestPosition = detail::worldPosition(skeleton, pose, chest);
            rotateWorld(skeleton, pose, chest,
                        lookAtDeltaQ(chestPosition, eyesPosition, eyesRotation,
                                     headForward, headGoal, bodyWeight));
        }
    }

    if (headWeight > 0.0f) {
        if (neck >= 0) {
            const Vec3 eyesPosition =
                lookAtEyesPosition(avatar, skeleton, pose, head);
            const Quat eyesRotation =
                lookAtRotation(avatar, HumanBone::Head,
                               detail::worldRotation(skeleton, pose, head));
            const Vec3 neckPosition = detail::worldPosition(skeleton, pose, neck);
            rotateWorld(skeleton, pose, neck,
                        lookAtDeltaQ(neckPosition, eyesPosition, eyesRotation,
                                     headForward, headGoal,
                                     0.5f * headWeight));
        }

        for (int iteration = 0; iteration < 3; ++iteration) {
            const Vec3 eyesPosition =
                lookAtEyesPosition(avatar, skeleton, pose, head);
            const Quat eyesRotation =
                lookAtRotation(avatar, HumanBone::Head,
                               detail::worldRotation(skeleton, pose, head));
            const Vec3 headPosition = detail::worldPosition(skeleton, pose, head);
            rotateWorld(skeleton, pose, head,
                        lookAtDeltaQ(headPosition, eyesPosition, eyesRotation,
                                     headForward, headGoal,
                                     headWeight * headWeight));
        }
    }

    if (eyeWeight > 0.0f) {
        const int eyeBones[] = {
            avatar.boneIndex(HumanBone::LeftEye),
            avatar.boneIndex(HumanBone::RightEye),
        };
        for (const int eye : eyeBones) {
            if (eye < 0) continue;
            const Vec3 eyesPosition = detail::worldPosition(skeleton, pose, eye);
            const HumanBone eyeBone = eye == avatar.boneIndex(HumanBone::LeftEye)
                                          ? HumanBone::LeftEye
                                          : HumanBone::RightEye;
            const Quat eyesRotation =
                lookAtRotation(avatar, eyeBone,
                               detail::worldRotation(skeleton, pose, eye));
            rotateWorld(skeleton, pose, eye,
                        lookAtDeltaQ(eyesPosition, eyesPosition, eyesRotation,
                                     lookAtForward(avatar, eyeBone), headGoal,
                                     eyeWeight));
        }
    }
}

void applyHumanoidFullBodySolve(const HumanoidSample& sample,
                                const HumanoidAvatar& avatar,
                                const Skeleton& skeleton, Pose& pose,
                                const Vec3& lookAtTarget,
                                const HumanoidLookAtWeights& lookAtWeights,
                                const HumanoidGoalWeights& goalWeights,
                                float humanScale) {
    applyHumanoidLookAt(avatar, skeleton, pose, lookAtTarget, lookAtWeights);
    applyHumanoidGoals(sample, avatar, skeleton, pose, goalWeights, humanScale);
}

}  // namespace anim
