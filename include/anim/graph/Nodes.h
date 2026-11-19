#pragma once

#include <functional>
#include <vector>

#include "anim/clip/Clip.h"
#include "anim/graph/PlayableNode.h"
#include "anim/jobs/IAnimationJob.h"
#include "anim/skeleton/AvatarMask.h"
#include "anim/skeleton/Skeleton.h"

// 内置节点类型。
//
// 文档：docs/06-Playable图.md §5
namespace anim {

// 采样单个 Clip，产出 Pose。
class ClipNode final : public PlayableNode {
public:
    ClipNode(const Clip& clip, const Skeleton& skeleton, const HumanoidAvatar* avatar = nullptr);

    PlayableNodeType type() const noexcept override { return PlayableNodeType::Clip; }
    const char* typeName() const noexcept override { return "Clip"; }
    bool hasPose() const noexcept override { return true; }

    void processFrame(float deltaTime) override;

    const Clip& clip() const noexcept { return *_clip; }
    const Skeleton& skeleton() const noexcept { return *_skeleton; }
    const HumanoidAvatar* avatar() const noexcept { return _avatar; }
    void setAvatar(const HumanoidAvatar* avatar) noexcept { _avatar = avatar; }

private:
    const Clip* _clip = nullptr;
    const Skeleton* _skeleton = nullptr;
    const HumanoidAvatar* _avatar = nullptr;
};

// N 路加权混合（AnimationMixerPlayable 等价物）。
// 端口权重来自 graph.setInputWeight；混合前不额外归一化（由上层保证总和≈1）。
class RetargetClipNode final : public PlayableNode {
public:
    RetargetClipNode(const Clip& clip, const Skeleton& sourceSkeleton,
                     const HumanoidAvatar& sourceAvatar, const Skeleton& targetSkeleton,
                     const HumanoidAvatar& targetAvatar);

    PlayableNodeType type() const noexcept override { return PlayableNodeType::RetargetClip; }
    const char* typeName() const noexcept override { return "RetargetClip"; }
    bool hasPose() const noexcept override { return true; }

    void processFrame(float deltaTime) override;

    const Clip& clip() const noexcept { return *_clip; }
    const Skeleton& sourceSkeleton() const noexcept { return *_sourceSkeleton; }
    const HumanoidAvatar& sourceAvatar() const noexcept { return *_sourceAvatar; }
    const Skeleton& targetSkeleton() const noexcept { return *_targetSkeleton; }
    const HumanoidAvatar& targetAvatar() const noexcept { return *_targetAvatar; }

private:
    const Clip* _clip = nullptr;
    const Skeleton* _sourceSkeleton = nullptr;
    const HumanoidAvatar* _sourceAvatar = nullptr;
    const Skeleton* _targetSkeleton = nullptr;
    const HumanoidAvatar* _targetAvatar = nullptr;
    Pose _sourcePose;
};

class MixerNode final : public PlayableNode {
public:
    PlayableNodeType type() const noexcept override { return PlayableNodeType::Mixer; }
    const char* typeName() const noexcept override { return "Mixer"; }
    bool hasPose() const noexcept override { return true; }

    void processFrame(float deltaTime) override;
    const Pose& outputPose() const override;

private:
    std::vector<const Pose*> _poses;
    std::vector<float> _weights;
    const Pose* _passthrough = nullptr;
};

// 层混合（AnimationLayerMixerPlayable 等价物）：覆盖 + 加法 + AvatarMask。
// 加法层把输入姿态视为“相对基准的增量”（translation 叠加、rotation 乘 delta、scale 乘增量）。
class LayerMixerNode final : public PlayableNode {
public:
    PlayableNodeType type() const noexcept override { return PlayableNodeType::LayerMixer; }
    const char* typeName() const noexcept override { return "LayerMixer"; }
    bool hasPose() const noexcept override { return true; }

    void setLayerAdditive(int layer, bool additive);
    bool isLayerAdditive(int layer) const;

    void setLayerMask(int layer, const AvatarMask* mask);
    const AvatarMask* layerMask(int layer) const;

    void processFrame(float deltaTime) override;
    const Pose& outputPose() const override;

private:
    void ensureLayer(int layer);

    std::vector<uint8_t> _additive;
    std::vector<const AvatarMask*> _masks;
    const Pose* _passthrough = nullptr;
};

// 承载用户行为（ScriptPlayable<T> 等价物）。Time 推进沿用基类。
class ScriptNode final : public PlayableNode {
public:
    using Callback = std::function<void(PlayableNode&, float)>;

    PlayableNodeType type() const noexcept override { return PlayableNodeType::Script; }
    const char* typeName() const noexcept override { return "Script"; }

    void setPrepareCallback(Callback callback) { _prepare = std::move(callback); }
    void setProcessCallback(Callback callback) { _process = std::move(callback); }

    void prepareFrame(float deltaTime) override;
    void processFrame(float deltaTime) override;

    bool hasPose() const noexcept override;
    const Pose& outputPose() const override;

private:
    Callback _prepare;
    Callback _process;
};

// 在 Pose 流上执行 IAnimationJob（AnimationScriptPlayable 等价物）。
class ScriptPoseNode final : public PlayableNode {
public:
    explicit ScriptPoseNode(const Skeleton& skeleton) : _skeleton(&skeleton) {}

    PlayableNodeType type() const noexcept override { return PlayableNodeType::ScriptPose; }
    const char* typeName() const noexcept override { return "ScriptPose"; }
    bool hasPose() const noexcept override { return true; }

    const Skeleton& skeleton() const noexcept { return *_skeleton; }
    void setSkeleton(const Skeleton& skeleton) noexcept { _skeleton = &skeleton; }

    void processFrame(float deltaTime) override;

    IAnimationJob* job = nullptr;
    IJobScheduler* scheduler = nullptr;  // 预留：P5 的任务调度器

private:
    const Skeleton* _skeleton = nullptr;
};

}  // namespace anim
