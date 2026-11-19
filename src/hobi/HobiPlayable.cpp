#include "anim/hobi/HobiPlayable.h"

#include <algorithm>
#include <cmath>

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/StateDictionary.h"
#include "anim/hobi/Transition.h"
#include "anim/hobi/rootmotion/RootMotion.h"
#include "anim/core/Assert.h"
#include "anim/graph/Nodes.h"
#include "anim/jobs/IAnimationJob.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {
namespace {
const Pose& emptyPose() {
    static const Pose pose;
    return pose;
}

HobiLayer* localLayer(HobiPlayable& root, HobiState* state) {
    if (state != nullptr && state->root() == &root) {
        HobiLayer* layer = state->layer();
        if (layer != nullptr) return layer;
    }
    return root.layers().getLayer(0);
}
}  // namespace

HobiPlayable::HobiPlayable() {
    _layerMixer = _graph.create<LayerMixerNode>();
    _graph.setOutput(_layerMixer);

    _rootNode = std::make_unique<HobiRootNode>();
    _rootNode->setRoot(this);
    _rootNode->setPlayable(_layerMixer);
    _rootNode->setWeight(1.0f);

    _layers = std::make_unique<LayerList>(*this, _layerMixer);
    _states = std::make_unique<StateDictionary>(*this);

    _layers->setCount(1);
    _graph.play();
}

HobiPlayable::~HobiPlayable() { destroyGraph(); }

void HobiPlayable::destroyGraph() {
    if (_layers) _layers->setCount(0);
    _layers.reset();
    _states.reset();
    _preUpdatables.clear();
    _postUpdatables.clear();
    _ownedPreUpdatables.clear();
    _rootNode.reset();
    _layerMixer = PlayableHandle{};
    _skeleton = nullptr;
    _rootMotionTarget = nullptr;
    _hasRootMotionPose = false;
    _previousPose.clear();
}

void HobiPlayable::createOutput(Skeleton& skeleton) { _skeleton = &skeleton; }

const Pose& HobiPlayable::outputPose() const {
    const PlayableNode* node = _graph.root();
    return node != nullptr ? node->outputPose() : emptyPose();
}

HobiState* HobiPlayable::play(const Clip& clip) {
    HobiState* state = _states->getOrCreate(clip);
    return state != nullptr ? play(state) : nullptr;
}

HobiState* HobiPlayable::play(const Clip& clip, float fadeDuration, FadeMode mode) {
    HobiState* state = _states->getOrCreate(clip);
    return state != nullptr ? play(state, fadeDuration, mode) : nullptr;
}

HobiState* HobiPlayable::play(HobiState* state) {
    HobiLayer* layer = localLayer(*this, state);
    return layer != nullptr ? layer->play(state) : nullptr;
}

HobiState* HobiPlayable::play(HobiState* state, float fadeDuration, FadeMode mode) {
    HobiLayer* layer = localLayer(*this, state);
    return layer != nullptr ? layer->play(state, fadeDuration, mode) : nullptr;
}

HobiState* HobiPlayable::play(ITransition& transition) {
    return play(transition, transition.fadeDuration(), transition.fadeMode());
}

HobiState* HobiPlayable::play(ITransition& transition, float fadeDuration, FadeMode mode) {
    HobiState* state = _states->getOrCreate(transition);
    if (state == nullptr) return nullptr;

    state = play(state, fadeDuration, mode);
    if (state != nullptr) transition.apply(*state);
    return state;
}

HobiState* HobiPlayable::tryPlay(Key key) {
    HobiState* state = nullptr;
    if (_states->tryGet(key, state)) return play(state);
    return nullptr;
}

void HobiPlayable::stop() {
    for (int i = 0; i < _layers->count(); ++i) _layers->getLayer(i)->stop();
}

bool HobiPlayable::isPlaying() const {
    if (!_graphPlaying) return false;
    for (int i = 0; i < _layers->count(); ++i) {
        if (_layers->getLayer(i)->isAnyStatePlaying()) return true;
    }
    return false;
}

bool HobiPlayable::isPlaying(const Clip& clip) const {
    if (!_graphPlaying) return false;
    for (int i = 0; i < _layers->count(); ++i) {
        if (_layers->getLayer(i)->isPlayingClip(clip)) return true;
    }
    return false;
}

float HobiPlayable::totalWeight() const {
    float total = 0.0f;
    for (int i = 0; i < _layers->count(); ++i) total += _layers->getLayer(i)->weight();
    return total;
}

void HobiPlayable::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    if (_layers != nullptr) _layers->gatherAnimationClips(clips);
}

void HobiPlayable::setSpeed(float value) {
    ANIM_CHECK_MSG(std::isfinite(value), "speed must be finite");
    _speed = value;
    if (_rootNode) _rootNode->setSpeed(value);
}

void HobiPlayable::pauseGraph() {
    _graphPlaying = false;
    _graph.stop();
}

void HobiPlayable::unpauseGraph() {
    _graphPlaying = true;
    _graph.play();
}

void HobiPlayable::requirePreUpdate(IUpdatable* updatable) {
    if (updatable == nullptr) return;
    for (IUpdatable* entry : _preUpdatables) {
        if (entry == updatable) return;
    }
    _preUpdatables.push_back(updatable);
}

void HobiPlayable::cancelPreUpdate(IUpdatable* updatable) {
    _preUpdatables.erase(std::remove(_preUpdatables.begin(), _preUpdatables.end(), updatable),
                         _preUpdatables.end());
}

void HobiPlayable::requirePostUpdate(IUpdatable* updatable) {
    if (updatable == nullptr) return;
    for (IUpdatable* entry : _postUpdatables) {
        if (entry == updatable) return;
    }
    _postUpdatables.push_back(updatable);
}

void HobiPlayable::cancelPostUpdate(IUpdatable* updatable) {
    _postUpdatables.erase(std::remove(_postUpdatables.begin(), _postUpdatables.end(), updatable),
                          _postUpdatables.end());
}

void HobiPlayable::requireOwnedPreUpdate(std::unique_ptr<IUpdatable> updatable) {
    if (updatable == nullptr) return;
    requirePreUpdate(updatable.get());
    _ownedPreUpdatables.push_back(std::move(updatable));
}

IUpdatable* HobiPlayable::preUpdatable(int index) const {
    if (index < 0 || index >= static_cast<int>(_preUpdatables.size())) return nullptr;
    return _preUpdatables[static_cast<std::size_t>(index)];
}

IUpdatable* HobiPlayable::postUpdatable(int index) const {
    if (index < 0 || index >= static_cast<int>(_postUpdatables.size())) return nullptr;
    return _postUpdatables[static_cast<std::size_t>(index)];
}

bool HobiPlayable::isPreUpdating(const IUpdatable* updatable) const {
    return std::find(_preUpdatables.begin(), _preUpdatables.end(), updatable) !=
           _preUpdatables.end();
}

bool HobiPlayable::isPostUpdating(const IUpdatable* updatable) const {
    return std::find(_postUpdatables.begin(), _postUpdatables.end(), updatable) !=
           _postUpdatables.end();
}

void HobiPlayable::setNodeTime(PlayableHandle handle, double time) {
    PlayableNode* node = _graph.get(handle);
    if (node != nullptr) node->setTime(time);
}

void HobiPlayable::setNodePlaying(PlayableHandle handle, bool playing) {
    PlayableNode* node = _graph.get(handle);
    if (node == nullptr) return;
    if (playing)
        node->play();
    else
        node->pause();
}

PlayableHandle HobiPlayable::insertOutputJob(IAnimationJob* job,
                                                  IJobScheduler* scheduler) {
    ANIM_CHECK_MSG(_skeleton != nullptr, "createOutput must be called before insertOutputJob");
    ANIM_CHECK_MSG(job != nullptr, "job is null");

    const PlayableHandle handle = _graph.create<ScriptPoseNode>(*_skeleton);
    auto* script = static_cast<ScriptPoseNode*>(_graph.get(handle));
    script->job = job;
    script->scheduler = scheduler;

    const PlayableHandle current = _graph.output();
    if (current.valid()) {
        _graph.connect(current, 0, handle, 0);
        _graph.setInputWeight(handle, 0, 1.0f);
    }
    _graph.setOutput(handle);
    return handle;
}

void HobiPlayable::removePlayable(PlayableHandle handle) {
    PlayableNode* node = _graph.get(handle);
    if (node == nullptr) return;

    const PlayableHandle child = node->inputCount() > 0 ? node->input(0) : PlayableHandle{};

    if (_graph.output() == handle) {
        _graph.disconnect(handle, 0);
        _graph.setOutput(child);
        _graph.destroy(handle);
        return;
    }

    for (PlayableHandle other : _graph.nodeHandles()) {
        if (other == handle) continue;
        PlayableNode* parent = _graph.get(other);
        if (parent == nullptr) continue;

        for (int port = 0; port < parent->inputCount(); ++port) {
            if (parent->input(port) != handle) continue;
            if (child.valid()) {
                _graph.connect(child, 0, other, port);
            } else {
                _graph.disconnect(other, port);
            }
        }
    }

    _graph.destroy(handle);
}

void HobiPlayable::setRootMotionTarget(IRootMotionTarget* target) {
    _rootMotionTarget = target;
    _hasRootMotionPose = false;
    _rootMotionDelta = RootMotionDelta{};
}

void HobiPlayable::setRootMotionRootBone(int bone) noexcept {
    _rootMotionRootBone = bone;
    _hasRootMotionPose = false;
}

void HobiPlayable::runPreUpdates() {
    // 与原项目 UpdateAll 一致：每帧对列表做一次遍历。
    // needsMore=false 的节点从列表中移除（除非它需要持续更新，例如正在播放的 State）。
    // 注意：update 内允许取消自身（CustomFade 完成时会 delete this），因此更新后按指针重新定位。
    for (std::size_t i = 0; i < _preUpdatables.size();) {
        IUpdatable* updatable = _preUpdatables[i];
        bool needsMore = false;
        updatable->update(needsMore);

        const auto it = std::find(_preUpdatables.begin(), _preUpdatables.end(), updatable);
        if (it == _preUpdatables.end()) {
            // 已在 update 中移除（可能已销毁）；不要推进 i，先处理被换到该位置的元素。
            continue;
        }

        if (needsMore || updatable->requiresContinuousUpdate()) {
            ++i;
            continue;
        }

        const std::size_t index = static_cast<std::size_t>(it - _preUpdatables.begin());
        // 保持列表顺序：顺序错误会导致混音器在子状态之后才更新权重。
        _preUpdatables.erase(it);
        if (index < i) --i;
    }
}

void HobiPlayable::runPostUpdates() {
    _runningPostUpdate = true;

    // update 内允许取消/注册其它 post-update（ExitEvent 会在回调后移除自身），
    // 因此用快照并跳过已经不在列表中的对象，避免迭代器失效。
    const std::vector<IUpdatable*> snapshot = _postUpdatables;
    for (IUpdatable* updatable : snapshot) {
        if (!isPostUpdating(updatable)) continue;
        bool ignored = false;
        updatable->update(ignored);
    }

    _runningPostUpdate = false;
}

void HobiPlayable::evaluate(float deltaTime) {
    if (!_graphPlaying) return;
    ANIM_CHECK_MSG(std::isfinite(deltaTime), "deltaTime must be finite");

    _deltaTime = deltaTime;
    ++_frameId;

    runPreUpdates();
    _graph.evaluate(deltaTime);
    runPostUpdates();

    if (_rootMotionTarget == nullptr || _skeleton == nullptr) {
        _hasRootMotionPose = false;
        return;
    }

    const Pose& current = outputPose();
    if (!_hasRootMotionPose || current.local.size() != _previousPose.local.size()) {
        _previousPose = current;
        _hasRootMotionPose = true;
        return;
    }

    _rootMotionDelta =
        extractRootMotion(_previousPose, current, *_skeleton, _rootMotionRootBone);
    if (_rootMotionTarget->enabled()) {
        _rootMotionTarget->applyRootMotion(_rootMotionDelta);
    }
    _previousPose = current;
}

}  // namespace anim
