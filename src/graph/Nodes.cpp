#include "anim/graph/Nodes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "anim/avatar/HumanoidIK.h"
#include "anim/core/Assert.h"
#include "anim/jobs/JobScheduler.h"
#include "anim/skeleton/Blend.h"

namespace anim {
namespace {

void blendBoneOverride(Transform& out, const Transform& layer, float weight) {
    out.translation = out.translation + (layer.translation - out.translation) * weight;
    out.scale = out.scale + (layer.scale - out.scale) * weight;
    out.rotation = safeNormalize(
        glm::slerp(safeNormalize(out.rotation), safeNormalize(layer.rotation), weight));
}

// 加法层：把 layer 当作相对基准的增量（identity = 无变化）。
void blendBoneAdditive(Transform& out, const Transform& layer, float weight) {
    out.translation += layer.translation * weight;
    out.scale *= Vec3(1.0f) + (layer.scale - Vec3(1.0f)) * weight;
    out.rotation = safeNormalize(
        out.rotation * glm::slerp(kIdentityQuat, safeNormalize(layer.rotation), weight));
}

}  // namespace

// ---------------------------------------------------------------------------
// ClipNode
// ---------------------------------------------------------------------------

ClipNode::ClipNode(const Clip& clip, const Skeleton& skeleton, const HumanoidAvatar* avatar)
    : _clip(&clip), _skeleton(&skeleton), _avatar(avatar) {
    setDuration(static_cast<double>(clip.length));
    setLooping(clip.looping);
}

void ClipNode::processFrame(float /*deltaTime*/) {
    ANIM_CHECK_MSG(_clip != nullptr, "clip is null");
    ANIM_CHECK_MSG(_skeleton != nullptr, "skeleton is null");

    float sampleTime = static_cast<float>(time());
    if (_clip->length > 0.0f) {
        if (_clip->looping) {
            sampleTime = std::fmod(sampleTime, _clip->length);
            if (sampleTime < 0.0f) sampleTime += _clip->length;
        } else {
            sampleTime = std::clamp(sampleTime, 0.0f, _clip->length);
        }
    }

    _output.resizeLike(*_skeleton);
    sampleClip(*_clip, sampleTime, *_skeleton, _avatar, _output);
}

// ---------------------------------------------------------------------------
// RetargetClipNode
// ---------------------------------------------------------------------------

RetargetClipNode::RetargetClipNode(const Clip& clip, const Skeleton& sourceSkeleton,
                                   const HumanoidAvatar& sourceAvatar,
                                   const Skeleton& targetSkeleton,
                                   const HumanoidAvatar& targetAvatar)
    : _clip(&clip),
      _sourceSkeleton(&sourceSkeleton),
      _sourceAvatar(&sourceAvatar),
      _targetSkeleton(&targetSkeleton),
      _targetAvatar(&targetAvatar) {
    setDuration(static_cast<double>(clip.length));
    setLooping(clip.looping);
}

void RetargetClipNode::processFrame(float /*deltaTime*/) {
    ANIM_CHECK_MSG(_clip != nullptr, "clip is null");
    ANIM_CHECK_MSG(_clip->type == ClipType::BoneLocal,
                   "RetargetClipNode requires a BoneLocal clip");
    ANIM_CHECK_MSG(_sourceSkeleton != nullptr, "source skeleton is null");
    ANIM_CHECK_MSG(_sourceAvatar != nullptr, "source avatar is null");
    ANIM_CHECK_MSG(_targetSkeleton != nullptr, "target skeleton is null");
    ANIM_CHECK_MSG(_targetAvatar != nullptr, "target avatar is null");

    float sampleTime = static_cast<float>(time());
    if (_clip->length > 0.0f) {
        if (_clip->looping) {
            sampleTime = std::fmod(sampleTime, _clip->length);
            if (sampleTime < 0.0f) sampleTime += _clip->length;
        } else {
            sampleTime = std::clamp(sampleTime, 0.0f, _clip->length);
        }
    }

    _sourcePose.resizeLike(*_sourceSkeleton);
    sampleClip(*_clip, sampleTime, *_sourceSkeleton, _sourceAvatar, _sourcePose);

    _output.resizeLike(*_targetSkeleton);
    retargetHumanoidLocalPose(*_sourceAvatar, *_sourceSkeleton, _sourcePose,
                              *_targetAvatar, *_targetSkeleton, _output);
}

// ---------------------------------------------------------------------------
// MixerNode
// ---------------------------------------------------------------------------

void MixerNode::processFrame(float /*deltaTime*/) {
    _poses.clear();
    _weights.clear();

    for (int port = 0; port < inputCount(); ++port) {
        PlayableNode* child = inputNode(port);
        if (child == nullptr || !child->hasPose()) continue;

        const float weight = inputWeight(port);
        if (weight <= 0.0f) continue;

        _poses.push_back(&child->outputPose());
        _weights.push_back(weight);
    }

    if (_poses.empty()) {
        _passthrough = nullptr;
        _output.local.clear();
        return;
    }

    if (_poses.size() == 1 && _weights[0] == 1.0f) {
        _passthrough = _poses[0];
        _output.local.clear();
        return;
    }

    _passthrough = nullptr;
    blendPoses(_poses, _weights, _output);
}

const Pose& MixerNode::outputPose() const {
    return _passthrough != nullptr ? *_passthrough : _output;
}

// ---------------------------------------------------------------------------
// LayerMixerNode
// ---------------------------------------------------------------------------

void LayerMixerNode::ensureLayer(int layer) {
    ANIM_CHECK_MSG(layer >= 0, "layer must be non-negative");
    const std::size_t count = static_cast<std::size_t>(layer) + 1;
    if (_additive.size() < count) _additive.resize(count, 0);
    if (_masks.size() < count) _masks.resize(count, nullptr);
}

void LayerMixerNode::setLayerAdditive(int layer, bool additive) {
    ensureLayer(layer);
    _additive[static_cast<std::size_t>(layer)] = additive ? 1 : 0;
}

bool LayerMixerNode::isLayerAdditive(int layer) const {
    if (layer < 0 || static_cast<std::size_t>(layer) >= _additive.size()) return false;
    return _additive[static_cast<std::size_t>(layer)] != 0;
}

void LayerMixerNode::setLayerMask(int layer, const AvatarMask* mask) {
    ensureLayer(layer);
    _masks[static_cast<std::size_t>(layer)] = mask;
}

const AvatarMask* LayerMixerNode::layerMask(int layer) const {
    if (layer < 0 || static_cast<std::size_t>(layer) >= _masks.size()) return nullptr;
    return _masks[static_cast<std::size_t>(layer)];
}

void LayerMixerNode::processFrame(float /*deltaTime*/) {
    _passthrough = nullptr;

    int poseCount = 0;
    const Pose* onlyPose = nullptr;
    for (int port = 0; port < inputCount(); ++port) {
        PlayableNode* child = inputNode(port);
        if (child == nullptr || !child->hasPose()) continue;
        const Pose& pose = child->outputPose();
        if (pose.local.empty()) continue;
        ++poseCount;
        onlyPose = &pose;
    }

    if (poseCount == 0) {
        _output.local.clear();
        return;
    }

    if (poseCount == 1) {
        _passthrough = onlyPose;
        _output.local.clear();
        return;
    }

    bool hasBase = false;
    for (int port = 0; port < inputCount(); ++port) {
        PlayableNode* child = inputNode(port);
        if (child == nullptr || !child->hasPose()) continue;

        const Pose& layerPose = child->outputPose();
        if (layerPose.local.empty()) continue;

        // 第一个可用输入作为基准层（Unity 第 0 层）。
        if (!hasBase) {
            _output = layerPose;
            hasBase = true;
            continue;
        }

        const float weight = inputWeight(port);
        if (weight <= 0.0f) continue;

        ANIM_CHECK_MSG(_output.local.size() == layerPose.local.size(),
                       "layer pose size mismatch");

        const bool additive = isLayerAdditive(port);
        const AvatarMask* mask = layerMask(port);

        for (std::size_t bone = 0; bone < _output.local.size(); ++bone) {
            float maskWeight = 1.0f;
            if (mask != nullptr) maskWeight = mask->weightFor(static_cast<int>(bone));

            const float alpha = weight * maskWeight;
            if (alpha <= 0.0f) continue;

            if (additive) {
                blendBoneAdditive(_output.local[bone], layerPose.local[bone], alpha);
            } else {
                blendBoneOverride(_output.local[bone], layerPose.local[bone], alpha);
            }
        }
    }

    if (!hasBase) _output.local.clear();
}

const Pose& LayerMixerNode::outputPose() const {
    return _passthrough != nullptr ? *_passthrough : _output;
}

// ---------------------------------------------------------------------------
// ScriptNode
// ---------------------------------------------------------------------------

void ScriptNode::prepareFrame(float deltaTime) {
    PlayableNode::prepareFrame(deltaTime);
    if (_prepare) _prepare(*this, deltaTime);
}

void ScriptNode::processFrame(float deltaTime) {
    if (_process) _process(*this, deltaTime);
}

bool ScriptNode::hasPose() const noexcept {
    if (inputCount() <= 0) return false;
    const PlayableNode* child = inputNode(0);
    return child != nullptr && child->hasPose();
}

const Pose& ScriptNode::outputPose() const {
    if (inputCount() > 0) {
        const PlayableNode* child = inputNode(0);
        if (child != nullptr && child->hasPose()) return child->outputPose();
    }
    return _output;
}

// ---------------------------------------------------------------------------
// ScriptPoseNode
// ---------------------------------------------------------------------------

void ScriptPoseNode::processFrame(float deltaTime) {
    ANIM_CHECK_MSG(_skeleton != nullptr, "skeleton is null");

    const PlayableNode* child = inputCount() > 0 ? inputNode(0) : nullptr;
    if (child != nullptr && child->hasPose() &&
        child->outputPose().local.size() == _skeleton->bones.size()) {
        _output = child->outputPose();
    } else {
        _output.resizeLike(*_skeleton);
        _output.resetToRest(*_skeleton);
    }

    if (job != nullptr) {
        if (scheduler != nullptr) {
            const JobHandle handle = scheduler->schedule(
                [this, deltaTime] { job->processAnimation(_output, *_skeleton, deltaTime); });
            scheduler->complete(handle);
        } else {
            job->processAnimation(_output, *_skeleton, deltaTime);
        }
    }
}

}  // namespace anim
