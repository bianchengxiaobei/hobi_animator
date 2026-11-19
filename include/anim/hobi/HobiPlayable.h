#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "anim/hobi/AnimationClipCollection.h"
#include "anim/hobi/HobiNode.h"
#include "anim/hobi/FadeMode.h"
#include "anim/core/IUpdatable.h"
#include "anim/graph/PlayableGraph.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/RootMotion.h"

// HobiPlayable：动画图根节点。
//
// 文档：docs/07-Hobi门面.md §1
// 图结构：LayerMixerNode（输出根）<- 每个 HobiLayer 的 MixerNode <- 各个 State 的 ClipNode。
namespace anim {

class HobiLayer;
class HobiState;
struct Clip;
struct HumanoidAvatar;
struct IAnimationJob;
class IEventObserver;
class IJobScheduler;
class IRootMotionTarget;
class ITransition;
class LayerList;
class PlayableNode;
class Skeleton;
class StateDictionary;
struct Key;

// 根节点：只承载速度/权重，不参与图求值。
class HobiRootNode final : public HobiNode {
public:
    bool keepChildrenConnected() const noexcept override { return true; }
    void update(bool& needsMoreUpdates) override { needsMoreUpdates = false; }

protected:
    // 根节点没有父节点、也没有图输出：权重/速度直接被 EffectiveWeight/EffectiveSpeed 读取，
    // 不需要进入 pre-update 列表（否则会通过 swap-remove 扰乱子状态的更新顺序）。
    void setWeightDirty() noexcept override {}
};

class HobiPlayable : public IAnimationClipCollection {
public:
    HobiPlayable();
    ~HobiPlayable();

    HobiPlayable(const HobiPlayable&) = delete;
    HobiPlayable& operator=(const HobiPlayable&) = delete;

    static HobiPlayable* create() { return new HobiPlayable(); }
    void destroyGraph();

    // ---- 输出 ----
    void createOutput(Skeleton& skeleton);
    void setAvatar(const HumanoidAvatar* avatar) noexcept { _avatar = avatar; }
    Skeleton* skeleton() const noexcept { return _skeleton; }
    const HumanoidAvatar* avatar() const noexcept { return _avatar; }
    const Pose& outputPose() const;

    // ---- 播放 ----
    HobiState* play(const Clip& clip);
    HobiState* play(const Clip& clip, float fadeDuration, FadeMode mode);
    HobiState* play(HobiState* state);
    HobiState* play(HobiState* state, float fadeDuration, FadeMode mode);
    HobiState* play(ITransition& transition);
    HobiState* play(ITransition& transition, float fadeDuration, FadeMode mode);
    HobiState* tryPlay(Key key);
    void stop();

    bool isPlaying() const;
    bool isPlaying(const Clip& clip) const;
    float totalWeight() const;
    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

    // ---- 速度/暂停 ----
    float speed() const noexcept { return _speed; }
    void setSpeed(float value);
    bool isGraphPlaying() const noexcept { return _graphPlaying; }
    void pauseGraph();
    void unpauseGraph();

    void setEventObserver(IEventObserver* observer) noexcept;
    IEventObserver* eventObserver() const noexcept { return _eventObserver; }

    // ---- 更新 ----
    void evaluate(float deltaTime);
    void update(float deltaTime) { evaluate(deltaTime); }

    // ---- 更新列表 ----
    void requirePreUpdate(IUpdatable* updatable);
    void cancelPreUpdate(IUpdatable* updatable);
    void requirePostUpdate(IUpdatable* updatable);
    void cancelPostUpdate(IUpdatable* updatable);

    // 由 playable 持有生命周期（例如调试用的 DontAllowFade）。
    void requireOwnedPreUpdate(std::unique_ptr<IUpdatable> updatable);

    int preUpdatableCount() const noexcept {
        return static_cast<int>(_preUpdatables.size());
    }
    IUpdatable* preUpdatable(int index) const;
    int postUpdatableCount() const noexcept {
        return static_cast<int>(_postUpdatables.size());
    }
    IUpdatable* postUpdatable(int index) const;
    bool isPreUpdating(const IUpdatable* updatable) const;
    bool isPostUpdating(const IUpdatable* updatable) const;

    bool isRunningPostUpdate() const noexcept { return _runningPostUpdate; }
    uint64_t frameId() const noexcept { return _frameId; }
    float deltaTime() const noexcept { return _deltaTime; }

    // ---- 图 ----
    PlayableGraph& graph() noexcept { return _graph; }
    const PlayableGraph& graph() const noexcept { return _graph; }
    PlayableHandle playable() const noexcept { return _layerMixer; }
    PlayableNode* node(PlayableHandle handle) const noexcept { return _graph.get(handle); }
    HobiRootNode* rootNode() const noexcept { return _rootNode.get(); }

    PlayableHandle insertOutputJob(IAnimationJob* job, IJobScheduler* scheduler = nullptr);
    void removePlayable(PlayableHandle handle);

    // ---- 根运动 ----
    void setRootMotionTarget(IRootMotionTarget* target);
    IRootMotionTarget* rootMotionTarget() const noexcept { return _rootMotionTarget; }
    void setRootMotionRootBone(int bone) noexcept;
    int rootMotionRootBone() const noexcept { return _rootMotionRootBone; }
    const RootMotionDelta& rootMotionDelta() const noexcept { return _rootMotionDelta; }

    void setNodeTime(PlayableHandle handle, double time);
    void setNodePlaying(PlayableHandle handle, bool playing);

    LayerList& layers() noexcept { return *_layers; }
    const LayerList& layers() const noexcept { return *_layers; }
    StateDictionary& states() noexcept { return *_states; }
    const StateDictionary& states() const noexcept { return *_states; }

    float defaultFadeDuration = 0.25f;
    bool skipFirstFade = true;

private:
    void runPreUpdates();
    void runPostUpdates();

    PlayableGraph _graph;
    PlayableHandle _layerMixer{};
    std::unique_ptr<HobiRootNode> _rootNode;
    std::unique_ptr<LayerList> _layers;
    std::unique_ptr<StateDictionary> _states;

    Skeleton* _skeleton = nullptr;
    const HumanoidAvatar* _avatar = nullptr;

    IRootMotionTarget* _rootMotionTarget = nullptr;
    int _rootMotionRootBone = -1;
    bool _hasRootMotionPose = false;
    Pose _previousPose;
    RootMotionDelta _rootMotionDelta;

    std::vector<IUpdatable*> _preUpdatables;
    std::vector<IUpdatable*> _postUpdatables;
    std::vector<std::unique_ptr<IUpdatable>> _ownedPreUpdatables;

    uint64_t _frameId = 0;
    float _deltaTime = 0.0f;
    float _speed = 1.0f;
    bool _graphPlaying = true;
    bool _runningPostUpdate = false;
    IEventObserver* _eventObserver = nullptr;
};

}  // namespace anim
