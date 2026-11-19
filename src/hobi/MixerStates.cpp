#include "anim/hobi/MixerStates.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/ClipState.h"
#include "anim/hobi/Transition.h"
#include "anim/core/Assert.h"
#include "anim/graph/Nodes.h"

namespace anim {

int ManualMixerState::defaultChildCapacity = 8;
bool ManualMixerState::synchronizeNewChildren = true;
float ManualMixerState::minimumSynchronizeChildrenWeight = 0.01f;

namespace {

bool isChildOf(const HobiNode* child, const HobiNode* parent) {
    while (child != nullptr) {
        child = child->parent();
        if (child == parent) return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// ManualMixerState
// ---------------------------------------------------------------------------

ManualMixerState::ManualMixerState(HobiPlayable* root) : HobiState(root) {}

ManualMixerState::~ManualMixerState() = default;

HobiState* ManualMixerState::getChild(int index) const {
    if (index < 0 || index >= _childCount) return nullptr;
    return _children[static_cast<std::size_t>(index)];
}

void ManualMixerState::setChildCapacity(int value) {
    ANIM_CHECK_MSG(value >= 0, "Child capacity must be non-negative");
    const int current = childCapacity();
    if (value == current) return;

    if (value > current) {
        _children.resize(static_cast<std::size_t>(value), nullptr);
        if (_playable.valid() && root() != nullptr)
            root()->graph().setInputCount(_playable, value);
    } else {
        while (_childCount > value) {
            HobiState* child = _children[static_cast<std::size_t>(_childCount - 1)];
            onRemoveChild(child);
            child->destroy();
        }
        _children.resize(static_cast<std::size_t>(value), nullptr);
        if (_playable.valid() && root() != nullptr)
            root()->graph().setInputCount(_playable, value);
    }

    onChildCapacityChanged();
}

void ManualMixerState::ensureRemainingChildCapacity(int minimumCapacity) {
    const int target = _childCount + minimumCapacity;
    if (childCapacity() >= target) return;

    int capacity = std::max(childCapacity(), defaultChildCapacity);
    while (capacity < target) capacity *= 2;
    setChildCapacity(capacity);
}

void ManualMixerState::add(HobiState* state) {
    ANIM_CHECK_MSG(state != nullptr, "state is null");
    ANIM_CHECK_MSG(state->parent() == nullptr, "state already has a parent");

    state->setRoot(root());
    state->setIndex(_childCount);
    onAddChild(state);
}

ClipState* ManualMixerState::add(const Clip& clip) {
    auto* state = new ClipState(root(), clip);
    add(state);
    return state;
}

HobiState* ManualMixerState::add(ITransition& transition) {
    if (root() == nullptr) return nullptr;
    HobiState* state = transition.createState(*root());
    if (state == nullptr) return nullptr;
    add(state);
    transition.apply(*state);
    return state;
}

void ManualMixerState::addRange(const std::vector<const Clip*>& clips) {
    ensureRemainingChildCapacity(static_cast<int>(clips.size()));
    for (const Clip* clip : clips) {
        if (clip != nullptr) add(*clip);
    }
}

void ManualMixerState::onAddChild(HobiState* state) {
    ANIM_CHECK_MSG(state->index() == _childCount,
                   "Mixer children must be added in sequence starting from 0");

    if (_childCount >= childCapacity()) ensureRemainingChildCapacity(1);

    _children[static_cast<std::size_t>(_childCount)] = state;
    ++_childCount;

    state->setParent(this);

    if (_playable.valid() && root() != nullptr) {
        PlayableGraph& g = root()->graph();
        if (!state->playable().valid()) state->createPlayable();
        if (state->playable().valid()) {
            state->connectToGraph();
            g.setInputWeight(_playable, state->index(), state->weight());
        }
    }

    if (synchronizeNewChildren) synchronize(state);

    state->setIsPlaying(isPlaying());
}

void ManualMixerState::onRemoveChild(HobiState* state) {
    const int index = state->index();
    ANIM_CHECK_MSG(index >= 0 && index < _childCount, "child index out of range");

    if (root() != nullptr && _playable.valid()) root()->graph().disconnect(_playable, index);

    state->disconnectFromGraph();
    state->setParent(nullptr);
    state->setIndex(-1);
    dontSynchronize(state);

    // 后续子状态整体前移一位（对齐 ManualMixerState.cs:393-412）。
    for (int i = index + 1; i < _childCount; ++i) {
        HobiState* moved = _children[static_cast<std::size_t>(i)];
        moved->disconnectFromGraph();
        moved->setIndex(i - 1);
        _children[static_cast<std::size_t>(i - 1)] = moved;
        if (moved->playable().valid()) moved->connectToGraph();
    }

    _children[static_cast<std::size_t>(_childCount - 1)] = nullptr;
    --_childCount;
}

void ManualMixerState::remove(int index, bool destroy) {
    ANIM_CHECK_MSG(index >= 0 && index < _childCount, "child index out of range");
    remove(_children[static_cast<std::size_t>(index)], destroy);
}

void ManualMixerState::remove(HobiState* child, bool destroy) {
    if (child == nullptr || child->parent() != this) return;
    onRemoveChild(child);
    if (destroy) child->destroy();
}

void ManualMixerState::set(int index, HobiState* child, bool destroyPrevious) {
    ANIM_CHECK_MSG(index >= 0 && index < _childCount, "child index out of range");
    ANIM_CHECK_MSG(child != nullptr, "child is null");

    HobiState* previous = _children[static_cast<std::size_t>(index)];
    if (previous == child) return;

    if (child->parent() != nullptr) {
        child->disconnectFromGraph();
        child->setParent(nullptr);
    }

    previous->disconnectFromGraph();
    dontSynchronize(previous);
    previous->setParent(nullptr);
    previous->setIndex(-1);

    child->setRoot(root());
    child->setParent(this);
    child->setIndex(index);
    _children[static_cast<std::size_t>(index)] = child;

    if (_playable.valid() && root() != nullptr) {
        PlayableGraph& g = root()->graph();
        if (!child->playable().valid()) child->createPlayable();
        if (child->playable().valid()) {
            child->connectToGraph();
            g.setInputWeight(_playable, index, child->weight());
        }
    }

    if (synchronizeNewChildren) synchronize(child);

    if (destroyPrevious) previous->destroy();
}

ClipState* ManualMixerState::set(int index, const Clip& clip, bool destroyPrevious) {
    auto* state = new ClipState(root(), clip);
    set(index, state, destroyPrevious);
    return state;
}

int ManualMixerState::indexOf(const HobiState* child) const {
    for (int i = 0; i < _childCount; ++i) {
        if (_children[static_cast<std::size_t>(i)] == child) return i;
    }
    return -1;
}

void ManualMixerState::destroyChildren() {
    while (_childCount > 0) {
        HobiState* child = _children[static_cast<std::size_t>(_childCount - 1)];
        onRemoveChild(child);
        child->destroy();
    }
    _synchronizedChildren.clear();
}

void ManualMixerState::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    for (int i = _childCount - 1; i >= 0; --i) {
        const HobiState* child = _children[static_cast<std::size_t>(i)];
        if (child != nullptr) child->gatherAnimationClips(clips);
    }
}

// ---------------------------------------------------------------------------
// 时间/时长
// ---------------------------------------------------------------------------

float ManualMixerState::length() const {
    float totalWeight = 0.0f;
    float length = 0.0f;

    for (int i = _childCount - 1; i >= 0; --i) {
        const HobiState* child = _children[static_cast<std::size_t>(i)];
        if (child == nullptr) continue;

        const float weight = child->weight();
        if (weight == 0.0f) continue;

        const float childLength = child->length();
        if (childLength == 0.0f) continue;

        totalWeight += weight;
        length += childLength * weight;
    }

    return totalWeight > 0.0f ? length / totalWeight : 0.0f;
}

bool ManualMixerState::isLooping() const noexcept {
    for (int i = _childCount - 1; i >= 0; --i) {
        const HobiState* child = _children[static_cast<std::size_t>(i)];
        if (child != nullptr && child->isLooping()) return true;
    }
    return false;
}

double ManualMixerState::timeD() const noexcept {
    float totalWeight = 0.0f;
    float normalizedTime = 0.0f;
    float lengthValue = 0.0f;

    for (int i = _childCount - 1; i >= 0; --i) {
        const HobiState* child = _children[static_cast<std::size_t>(i)];
        if (child == nullptr) continue;

        const float weight = child->weight();
        if (weight == 0.0f) continue;

        const float childLength = child->length();
        if (childLength == 0.0f) continue;

        totalWeight += weight;
        normalizedTime += static_cast<float>(child->timeD() / childLength) * weight;
        lengthValue += childLength * weight;
    }

    if (totalWeight == 0.0f) return HobiState::timeD();

    totalWeight *= totalWeight;
    return static_cast<double>(normalizedTime) * lengthValue / totalWeight;
}

void ManualMixerState::setTimeD(double value) {
    HobiState::setTimeD(value);

    if (value == 0.0) {
        for (int i = _childCount - 1; i >= 0; --i) {
            if (_children[static_cast<std::size_t>(i)] != nullptr)
                _children[static_cast<std::size_t>(i)]->setTimeD(0.0);
        }
        return;
    }

    const float lengthValue = length();
    if (lengthValue == 0.0f) {
        for (int i = _childCount - 1; i >= 0; --i) {
            if (_children[static_cast<std::size_t>(i)] != nullptr)
                _children[static_cast<std::size_t>(i)]->setTimeD(0.0);
        }
        return;
    }

    const double normalized = value / static_cast<double>(lengthValue);
    for (int i = _childCount - 1; i >= 0; --i) {
        if (_children[static_cast<std::size_t>(i)] != nullptr)
            _children[static_cast<std::size_t>(i)]->setNormalizedTimeD(normalized);
    }
}

void ManualMixerState::moveTime(double time, bool normalized) {
    HobiState::moveTime(time, normalized);
    for (int i = _childCount - 1; i >= 0; --i) {
        if (_children[static_cast<std::size_t>(i)] != nullptr)
            _children[static_cast<std::size_t>(i)]->moveTime(time, normalized);
    }
}

// ---------------------------------------------------------------------------
// 权重
// ---------------------------------------------------------------------------

bool ManualMixerState::recalculateWeights() {
    if (!_weightsAreDirty) return false;
    forceRecalculateWeights();
    ANIM_CHECK_MSG(!_weightsAreDirty, "ForceRecalculateWeights must clear WeightsAreDirty");
    return true;
}

float ManualMixerState::calculateTotalWeight(const HobiState* const* states, int count) {
    float total = 0.0f;
    for (int i = count - 1; i >= 0; --i) total += states[i]->weight();
    return total;
}

void ManualMixerState::disableRemainingStates(int previousIndex) {
    for (int i = previousIndex + 1; i < _childCount; ++i) {
        _children[static_cast<std::size_t>(i)]->setWeight(0.0f);
    }
}

void ManualMixerState::normalizeWeights(float totalWeight) {
    if (totalWeight == 1.0f) return;

    // 对齐 ManualMixerState.cs:1319-1328（不检查 0，保持与原文一致）。
    totalWeight = 1.0f / totalWeight;
    for (int i = _childCount - 1; i >= 0; --i) {
        HobiState* child = _children[static_cast<std::size_t>(i)];
        child->setWeight(child->weight() * totalWeight);
    }
}

// ---------------------------------------------------------------------------
// 同步
// ---------------------------------------------------------------------------

ManualMixerState* ManualMixerState::getParentMixer() {
    ManualMixerState* mixer = this;
    for (HobiNode* parent = this->parent(); parent != nullptr; parent = parent->parent()) {
        if (auto* parentMixer = dynamic_cast<ManualMixerState*>(parent)) mixer = parentMixer;
    }
    return mixer;
}

int ManualMixerState::synchronizedChildCount() const noexcept {
    return static_cast<int>(_synchronizedChildren.size());
}

bool ManualMixerState::isSynchronized(const HobiState* state) const {
    ManualMixerState* mixer = const_cast<ManualMixerState*>(this)->getParentMixer();
    const auto& list = mixer->_synchronizedChildren;
    return std::find(list.begin(), list.end(), state) != list.end();
}

void ManualMixerState::synchronize(HobiState* state) {
    if (state == nullptr) return;

    ManualMixerState* mixer = getParentMixer();
    ANIM_CHECK_MSG(isChildOf(state, mixer), "State is not a child of the mixer");

    // 如果 state 本身是混合器，把它的同步子状态提升到顶层（ManualMixerState.cs:943-954）。
    if (auto* stateMixer = dynamic_cast<ManualMixerState*>(state)) {
        if (!stateMixer->_synchronizedChildren.empty()) {
            std::vector<HobiState*> children;
            children.swap(stateMixer->_synchronizedChildren);
            for (HobiState* child : children) mixer->synchronize(child);
        }
        return;
    }

    auto& list = mixer->_synchronizedChildren;
    if (std::find(list.begin(), list.end(), state) != list.end()) return;

    list.push_back(state);
    if (mixer->root() != nullptr) mixer->root()->requirePreUpdate(mixer);
}

void ManualMixerState::dontSynchronize(HobiState* state) {
    ManualMixerState* mixer = getParentMixer();
    auto& list = mixer->_synchronizedChildren;
    auto it = std::find(list.begin(), list.end(), state);
    if (it == list.end()) return;

    list.erase(it);
    state->setPlaybackSpeedScale(1.0f);
}

void ManualMixerState::dontSynchronizeChildren() {
    ManualMixerState* mixer = getParentMixer();
    auto& list = mixer->_synchronizedChildren;

    if (mixer == this) {
        for (HobiState* state : list) state->setPlaybackSpeedScale(1.0f);
        list.clear();
        return;
    }

    for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i) {
        HobiState* state = list[static_cast<std::size_t>(i)];
        if (isChildOf(state, this)) {
            state->setPlaybackSpeedScale(1.0f);
            list.erase(list.begin() + i);
        }
    }
}

void ManualMixerState::initializeSynchronizedChildren(const std::vector<bool>& flags) {
    ANIM_CHECK_MSG(getParentMixer() == this,
                   "InitializeSynchronizedChildren cannot be used on a nested mixer");

    _synchronizedChildren.clear();

    const std::size_t flagCount = flags.size();
    for (int i = 0; i < _childCount; ++i) {
        const bool sync = static_cast<std::size_t>(i) >= flagCount ||
                          flags[static_cast<std::size_t>(i)];
        if (sync) synchronize(_children[static_cast<std::size_t>(i)]);
    }
}

float ManualMixerState::calculateRealEffectiveSpeed() const {
    // 对应 ManualMixerState.cs:1229-1241：图节点速度 × 父链图节点速度。
    // 本库没有独立于 Speed 的 Playable 速度，用 EffectiveSpeed（含 ExtrapolateSpeed 倍率）近似。
    return effectiveSpeed();
}

void ManualMixerState::applySynchronizeChildren(bool& needsMoreUpdates) {
    if (weight() == 0.0f || !isPlaying() || _synchronizedChildren.size() <= 1) return;
    if (root() == nullptr) return;

    needsMoreUpdates = true;

    const float deltaTime = root()->deltaTime() * calculateRealEffectiveSpeed();
    if (deltaTime == 0.0f) return;

    const int count = static_cast<int>(_synchronizedChildren.size());

    float totalWeight = 0.0f;
    float weightedNormalizedTime = 0.0f;
    float weightedNormalizedSpeed = 0.0f;

    for (int i = 0; i < count; ++i) {
        HobiState* state = _synchronizedChildren[static_cast<std::size_t>(i)];

        const float weight = state->weight();
        if (weight == 0.0f) continue;

        const float lengthValue = state->length();
        if (lengthValue == 0.0f) continue;

        totalWeight += weight;

        const float scaledWeight = weight / lengthValue;
        weightedNormalizedTime += state->time() * scaledWeight;
        weightedNormalizedSpeed += state->speed() * scaledWeight;
    }

    if (totalWeight < minimumSynchronizeChildrenWeight) {
        weightedNormalizedTime = 0.0f;
        weightedNormalizedSpeed = 0.0f;

        int nonZeroCount = 0;
        for (int i = 0; i < count; ++i) {
            HobiState* state = _synchronizedChildren[static_cast<std::size_t>(i)];
            const float lengthValue = state->length();
            if (lengthValue == 0.0f) continue;

            const float inverseLength = 1.0f / lengthValue;
            weightedNormalizedTime += state->time() * inverseLength;
            weightedNormalizedSpeed += state->speed() * inverseLength;
            ++nonZeroCount;
        }

        totalWeight = static_cast<float>(nonZeroCount);
    }

    if (totalWeight <= 0.0f) return;

    weightedNormalizedTime += deltaTime * weightedNormalizedSpeed;
    weightedNormalizedTime /= totalWeight;

    const float inverseDeltaTime = 1.0f / deltaTime;

    for (int i = 0; i < count; ++i) {
        HobiState* state = _synchronizedChildren[static_cast<std::size_t>(i)];
        const float lengthValue = state->length();
        if (lengthValue == 0.0f) continue;

        const float normalizedTime = static_cast<float>(state->timeD() / lengthValue);
        const float speed = (weightedNormalizedTime - normalizedTime) * lengthValue * inverseDeltaTime;

        // Original 直接改子节点 Playable 速度；父级（含 ExtrapolateSpeed）会再乘一次，
        // 因此这里只需用“相对自身 Speed 的倍率”表示同样的语义。
        const float baseSpeed = state->speed();
        if (baseSpeed != 0.0f) state->setPlaybackSpeedScale(speed / baseSpeed);
    }
}

// ---------------------------------------------------------------------------
// 图/更新/销毁
// ---------------------------------------------------------------------------

void ManualMixerState::createPlayable() {
    if (root() == nullptr) return;

    _playable = root()->graph().create<MixerNode>();
    root()->graph().setInputCount(_playable, childCapacity());
    connectChildren();
}

void ManualMixerState::connectChildren() {
    if (root() == nullptr || !_playable.valid()) return;

    PlayableGraph& g = root()->graph();
    for (int i = 0; i < _childCount; ++i) {
        HobiState* child = _children[static_cast<std::size_t>(i)];
        if (child == nullptr) continue;

        child->setRoot(root());
        child->setParent(this);
        child->setIndex(i);

        if (!child->playable().valid()) child->createPlayable();
        if (!child->playable().valid()) continue;

        child->connectToGraph();
        g.setInputWeight(_playable, i, child->weight());
    }
}

void ManualMixerState::update(bool& needsMoreUpdates) {
    HobiState::update(needsMoreUpdates);

    if (recalculateWeights()) {
        // 立即应用，避免子状态在下次 pre-update 前使用旧权重（ManualMixerState.cs:816-822）。
        for (int i = _childCount - 1; i >= 0; --i) {
            HobiState* child = _children[static_cast<std::size_t>(i)];
            if (child != nullptr) child->applyWeight();
        }
    }

    applySynchronizeChildren(needsMoreUpdates);
}

void ManualMixerState::onSetIsPlaying() {
    for (int i = _childCount - 1; i >= 0; --i) {
        HobiState* child = _children[static_cast<std::size_t>(i)];
        if (child != nullptr) child->setIsPlaying(isPlaying());
    }
}

void ManualMixerState::destroy() {
    destroyChildren();
    HobiState::destroy();
}

void ManualMixerState::copyStructureTo(ManualMixerState* clone) const {
    clone->setWeight(weight());
    clone->setSpeed(speed());
    clone->setPlaybackSpeedScale(playbackSpeedScale());
    clone->setApplyAnimatorIK(applyAnimatorIK());
    clone->setApplyFootIK(applyFootIK());

    for (int i = 0; i < _childCount; ++i) {
        HobiState* child = const_cast<HobiState*>(_children[static_cast<std::size_t>(i)]);
        if (child == nullptr) continue;

        HobiState* childClone = child->clone(clone->root());
        if (childClone == nullptr) continue;

        clone->add(childClone);
        childClone->setWeight(child->weight());
        childClone->setSpeed(child->speed());
    }
}

HobiState* ManualMixerState::clone(HobiPlayable* root) {
    auto* clone = new ManualMixerState(root);
    copyStructureTo(clone);
    return clone;
}

// ---------------------------------------------------------------------------
// LinearMixerState
// ---------------------------------------------------------------------------

void LinearMixerState::setExtrapolateSpeed(bool value) {
    if (_extrapolateSpeed == value) return;
    _extrapolateSpeed = value;
    setWeightsAreDirty(true);
}

void LinearMixerState::onThresholdsChanged() {
    _needToCheckThresholdSorting = true;
    MixerState<float>::onThresholdsChanged();
}

void LinearMixerState::assertThresholdsSorted() {
    _needToCheckThresholdSorting = false;

    ANIM_CHECK_MSG(hasThresholds(), "Thresholds have not been initialized");

    float previous = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < childCount(); ++i) {
        const float next = getThreshold(i);
        ANIM_CHECK_MSG(next > previous,
                       "Mixer thresholds must be sorted from lowest to highest with no equal values");
        previous = next;
    }
}

void LinearMixerState::forceRecalculateWeights() {
    _weightsAreDirty = false;

    if (_needToCheckThresholdSorting) assertThresholdsSorted();

    const int childCountValue = childCount();
    if (childCountValue == 0) {
        if (_extrapolateSpeed) setPlaybackSpeedScale(1.0f);
        return;
    }

    int index = 0;
    HobiState* previousState = _children[0];

    const float parameterValue = parameter();
    float previousThreshold = getThreshold(0);

    if (parameterValue <= previousThreshold) {
        disableRemainingStates(index);

        if (previousThreshold >= 0.0f) {
            previousState->setWeight(1.0f);
            if (_extrapolateSpeed && root() != nullptr) setPlaybackSpeedScale(1.0f);
            return;
        }
    } else {
        while (++index < childCountValue) {
            HobiState* nextState = _children[static_cast<std::size_t>(index)];
            const float nextThreshold = getThreshold(index);

            if (parameterValue > previousThreshold && parameterValue <= nextThreshold) {
                const float t =
                    (parameterValue - previousThreshold) / (nextThreshold - previousThreshold);
                previousState->setWeight(1.0f - t);
                nextState->setWeight(t);
                disableRemainingStates(index);
                if (_extrapolateSpeed && root() != nullptr) setPlaybackSpeedScale(1.0f);
                return;
            }

            previousState->setWeight(0.0f);
            previousState = nextState;
            previousThreshold = nextThreshold;
        }
    }

    previousState->setWeight(1.0f);

    if (_extrapolateSpeed) {
        setPlaybackSpeedScale(previousThreshold != 0.0f ? parameterValue / previousThreshold : 1.0f);
    }
}

LinearMixerState& LinearMixerState::assignLinearThresholds(float min, float max) {
    ANIM_CHECK_MSG(min < max, "min must be less than max");

    const int count = childCount();
    std::vector<float> thresholds(static_cast<std::size_t>(count));

    const float increment = count > 1 ? (max - min) / static_cast<float>(count - 1) : 0.0f;
    for (int i = 0; i < count; ++i) {
        thresholds[static_cast<std::size_t>(i)] =
            i < count - 1 ? min + static_cast<float>(i) * increment : max;
    }

    setThresholds(thresholds);
    return *this;
}

HobiState* LinearMixerState::clone(HobiPlayable* root) {
    auto* clone = new LinearMixerState(root);
    copyStructureTo(clone);
    clone->_thresholds = _thresholds;
    clone->_parameter = _parameter;
    clone->_extrapolateSpeed = _extrapolateSpeed;
    return clone;
}

// ---------------------------------------------------------------------------
// DirectionalMixerState
// ---------------------------------------------------------------------------

void DirectionalMixerState::onThresholdsChanged() {
    _blendFactorsDirty = true;
    MixerState<Vec2>::onThresholdsChanged();
}

void DirectionalMixerState::calculateBlendFactors(int childCountValue) {
    if (!_blendFactorsDirty) return;
    _blendFactorsDirty = false;

    if (static_cast<int>(_blendFactors.size()) != childCountValue) {
        _thresholdMagnitudes.resize(static_cast<std::size_t>(childCountValue));
        _blendFactors.assign(static_cast<std::size_t>(childCountValue),
                             std::vector<Vec2>(static_cast<std::size_t>(childCountValue)));
    }

    for (int i = 0; i < childCountValue; ++i) {
        _thresholdMagnitudes[static_cast<std::size_t>(i)] = glm::length(getThreshold(i));
    }

    for (int i = 0; i < childCountValue; ++i) {
        auto& blendFactors = _blendFactors[static_cast<std::size_t>(i)];

        const Vec2 thresholdI = getThreshold(i);
        const float magnitudeI = _thresholdMagnitudes[static_cast<std::size_t>(i)];

        for (int j = 0; j < childCountValue; ++j) {
            if (i == j) continue;

            const Vec2 thresholdJ = getThreshold(j);
            const float magnitudeJ = _thresholdMagnitudes[static_cast<std::size_t>(j)];

            const float averageMagnitude = (magnitudeI + magnitudeJ) * 0.5f;

            const float differenceIToJ = magnitudeJ - magnitudeI;
            const float angleIToJ = atan2(thresholdI.x * thresholdJ.y - thresholdI.y * thresholdJ.x,
                                          thresholdI.x * thresholdJ.x + thresholdI.y * thresholdJ.y);

            Vec2 polarIToJ(differenceIToJ / averageMagnitude, angleIToJ * angleFactor);
            const float squared = glm::dot(polarIToJ, polarIToJ);
            if (squared != 0.0f) polarIToJ *= 1.0f / squared;

            blendFactors[static_cast<std::size_t>(j)] = polarIToJ;
            _blendFactors[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = -polarIToJ;
        }
    }
}

void DirectionalMixerState::forceRecalculateWeights() {
    _weightsAreDirty = false;

    const int childCountValue = childCount();
    if (childCountValue == 0) return;
    if (childCountValue == 1) {
        _children[0]->setWeight(1.0f);
        return;
    }

    calculateBlendFactors(childCountValue);

    const float parameterMagnitude = glm::length(parameter());
    float totalWeight = 0.0f;

    for (int i = 0; i < childCountValue; ++i) {
        HobiState* state = _children[static_cast<std::size_t>(i)];
        if (state == nullptr) continue;

        const auto& blendFactors = _blendFactors[static_cast<std::size_t>(i)];

        const Vec2 thresholdI = getThreshold(i);
        const float magnitudeI = _thresholdMagnitudes[static_cast<std::size_t>(i)];

        const float differenceIToParameter = parameterMagnitude - magnitudeI;
        const float angleIToParameter =
            atan2(thresholdI.x * parameter().y - thresholdI.y * parameter().x,
                  thresholdI.x * parameter().x + thresholdI.y * parameter().y) *
            angleFactor;

        float weight = 1.0f;

        for (int j = 0; j < childCountValue; ++j) {
            if (j == i || _children[static_cast<std::size_t>(j)] == nullptr) continue;

            const float magnitudeJ = _thresholdMagnitudes[static_cast<std::size_t>(j)];
            const float averageMagnitude = (magnitudeJ + magnitudeI) * 0.5f;

            const Vec2 polarIToParameter(differenceIToParameter / averageMagnitude,
                                         angleIToParameter);

            const float newWeight = 1.0f - glm::dot(polarIToParameter,
                                                    blendFactors[static_cast<std::size_t>(j)]);
            if (weight > newWeight) weight = newWeight;
        }

        if (weight < 0.01f) weight = 0.0f;

        state->setWeight(weight);
        totalWeight += weight;
    }

    normalizeWeights(totalWeight);
}

HobiState* DirectionalMixerState::clone(HobiPlayable* root) {
    auto* clone = new DirectionalMixerState(root);
    copyStructureTo(clone);
    clone->_thresholds = _thresholds;
    clone->_parameter = _parameter;
    return clone;
}

// ---------------------------------------------------------------------------
// CartesianMixerState
// ---------------------------------------------------------------------------

void CartesianMixerState::onThresholdsChanged() {
    _blendFactorsDirty = true;
    MixerState<Vec2>::onThresholdsChanged();
}

void CartesianMixerState::calculateBlendFactors(int childCountValue) {
    if (!_blendFactorsDirty) return;
    _blendFactorsDirty = false;

    if (static_cast<int>(_blendFactors.size()) != childCountValue) {
        _blendFactors.assign(static_cast<std::size_t>(childCountValue),
                             std::vector<Vec2>(static_cast<std::size_t>(childCountValue)));
    }

    for (int i = 0; i < childCountValue; ++i) {
        auto& blendFactors = _blendFactors[static_cast<std::size_t>(i)];
        const Vec2 thresholdI = getThreshold(i);

        for (int j = i + 1; j < childCountValue; ++j) {
            Vec2 thresholdIToJ = getThreshold(j) - thresholdI;
            const float squared = glm::dot(thresholdIToJ, thresholdIToJ);
            if (squared == 0.0f) continue;

            thresholdIToJ *= 1.0f / squared;
            blendFactors[static_cast<std::size_t>(j)] = thresholdIToJ;
            _blendFactors[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] =
                -thresholdIToJ;
        }
    }
}

void CartesianMixerState::forceRecalculateWeights() {
    _weightsAreDirty = false;

    const int childCountValue = childCount();
    if (childCountValue == 0) return;
    if (childCountValue == 1) {
        _children[0]->setWeight(1.0f);
        return;
    }

    calculateBlendFactors(childCountValue);

    float totalWeight = 0.0f;

    for (int i = 0; i < childCountValue; ++i) {
        HobiState* state = _children[static_cast<std::size_t>(i)];
        if (state == nullptr) continue;

        const auto& blendFactors = _blendFactors[static_cast<std::size_t>(i)];
        const Vec2 thresholdToParameter = parameter() - getThreshold(i);

        float weight = 1.0f;

        for (int j = 0; j < childCountValue; ++j) {
            if (j == i || _children[static_cast<std::size_t>(j)] == nullptr) continue;

            const float newWeight =
                1.0f - glm::dot(thresholdToParameter, blendFactors[static_cast<std::size_t>(j)]);
            if (weight > newWeight) weight = newWeight;
        }

        if (weight < 0.01f) weight = 0.0f;

        state->setWeight(weight);
        totalWeight += weight;
    }

    normalizeWeights(totalWeight);
}

HobiState* CartesianMixerState::clone(HobiPlayable* root) {
    auto* clone = new CartesianMixerState(root);
    copyStructureTo(clone);
    clone->_thresholds = _thresholds;
    clone->_parameter = _parameter;
    return clone;
}

}  // namespace anim
