#include "anim/animancer/AnimancerAnimator.h"

#include "anim/animancer/AnimancerLayer.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/LayerList.h"
#include "anim/animancer/StateDictionary.h"
#include "anim/animancer/Transition.h"
#include "anim/core/Assert.h"
#include "anim/skeleton/Palette.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {

AnimancerAnimator::AnimancerAnimator() = default;

AnimancerAnimator::~AnimancerAnimator() { destroy(); }

void AnimancerAnimator::initialize(Skeleton& skeleton, const HumanoidAvatar* avatar,
                                   int layerCount) {
    destroy();

    _skeleton = &skeleton;
    _playable = std::make_unique<AnimancerPlayable>();
    _playable->createOutput(skeleton);
    _playable->setAvatar(avatar);

    const int count = layerCount > 0 ? layerCount : 1;
    _playable->layers().setCount(count);

    _palette.assign(static_cast<std::size_t>(skeleton.boneCount()), Mat4(1.0f));
}

void AnimancerAnimator::destroy() {
    _playable.reset();
    _skeleton = nullptr;
    _palette.clear();
}

void AnimancerAnimator::update(float deltaTime) {
    if (_playable == nullptr || _skeleton == nullptr) return;

    _playable->update(deltaTime);
    computePalette(*_skeleton, _playable->outputPose(), _palette);
}

void AnimancerAnimator::evaluate(float deltaTime) { update(deltaTime); }

void AnimancerAnimator::evaluate() { update(0.0f); }

float AnimancerAnimator::speed() const noexcept {
    return _playable != nullptr ? _playable->speed() : 1.0f;
}

void AnimancerAnimator::setSpeed(float value) {
    if (_playable != nullptr) _playable->setSpeed(value);
}

bool AnimancerAnimator::isGraphPlaying() const noexcept {
    return _playable != nullptr && _playable->isGraphPlaying();
}

void AnimancerAnimator::pauseGraph() {
    if (_playable != nullptr) _playable->pauseGraph();
}

void AnimancerAnimator::unpauseGraph() {
    if (_playable != nullptr) _playable->unpauseGraph();
}

RootMotionDelta AnimancerAnimator::consumeRootMotion() const {
    return _playable != nullptr ? _playable->rootMotionDelta() : RootMotionDelta{};
}

AnimancerState* AnimancerAnimator::play(const Clip& clip) {
    return _playable != nullptr ? _playable->play(clip) : nullptr;
}

AnimancerState* AnimancerAnimator::play(const Clip& clip, float fadeDuration, FadeMode mode) {
    return _playable != nullptr ? _playable->play(clip, fadeDuration, mode) : nullptr;
}

AnimancerState* AnimancerAnimator::play(ITransition& transition) {
    return _playable != nullptr ? _playable->play(transition) : nullptr;
}

AnimancerState* AnimancerAnimator::play(ITransition& transition, float fadeDuration,
                                        FadeMode mode) {
    return _playable != nullptr ? _playable->play(transition, fadeDuration, mode) : nullptr;
}

AnimancerState* AnimancerAnimator::play(AnimancerState* state) {
    if (_playable == nullptr || state == nullptr) return nullptr;
    return _playable->play(state);
}

AnimancerState* AnimancerAnimator::play(AnimancerState* state, float fadeDuration,
                                        FadeMode mode) {
    if (_playable == nullptr || state == nullptr) return nullptr;
    return _playable->play(state, fadeDuration, mode);
}

AnimancerState* AnimancerAnimator::tryPlay(Key key) {
    return _playable != nullptr ? _playable->tryPlay(key) : nullptr;
}

AnimancerState* AnimancerAnimator::tryPlay(Key key, float fadeDuration, FadeMode mode) {
    if (_playable == nullptr) return nullptr;

    AnimancerState* state = nullptr;
    if (!_playable->states().tryGet(key, state)) return nullptr;
    return _playable->play(state, fadeDuration, mode);
}

void AnimancerAnimator::stop() {
    if (_playable != nullptr) _playable->stop();
}

AnimancerState* AnimancerAnimator::stop(const Clip& clip) {
    return stop(Key::fromPointer(&clip));
}

AnimancerState* AnimancerAnimator::stop(Key key) {
    if (_playable == nullptr) return nullptr;

    AnimancerState* state = _playable->states().get(key);
    if (state != nullptr) state->stop();
    return state;
}

bool AnimancerAnimator::isPlaying() const {
    return _playable != nullptr && _playable->isPlaying();
}

bool AnimancerAnimator::isPlaying(const Clip& clip) const {
    return _playable != nullptr && _playable->isPlaying(clip);
}

bool AnimancerAnimator::isPlaying(Key key) const {
    if (_playable == nullptr) return false;

    AnimancerState* state = nullptr;
    return _playable->states().tryGet(key, state) && state->isPlaying();
}

bool AnimancerAnimator::isPlayingClip(const Clip& clip) const {
    return _playable != nullptr && _playable->isPlaying(clip);
}

AnimancerLayer* AnimancerAnimator::layer(int index) {
    return _playable != nullptr ? _playable->layers().getLayer(index) : nullptr;
}

void AnimancerAnimator::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    if (_playable != nullptr) _playable->gatherAnimationClips(clips);
}

}  // namespace anim
