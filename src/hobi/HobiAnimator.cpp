#include "anim/hobi/HobiAnimator.h"

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/StateDictionary.h"
#include "anim/hobi/Transition.h"
#include "anim/core/Assert.h"
#include "anim/skeleton/Palette.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {

HobiAnimator::HobiAnimator() = default;

HobiAnimator::~HobiAnimator() { destroy(); }

void HobiAnimator::initialize(Skeleton& skeleton, const HumanoidAvatar* avatar,
                                   int layerCount) {
    destroy();

    _skeleton = &skeleton;
    _playable = std::make_unique<HobiPlayable>();
    _playable->createOutput(skeleton);
    _playable->setAvatar(avatar);

    const int count = layerCount > 0 ? layerCount : 1;
    _playable->layers().setCount(count);

    _palette.assign(static_cast<std::size_t>(skeleton.boneCount()), Mat4(1.0f));
}

void HobiAnimator::destroy() {
    _playable.reset();
    _skeleton = nullptr;
    _palette.clear();
}

void HobiAnimator::update(float deltaTime) {
    if (_playable == nullptr || _skeleton == nullptr) return;

    _playable->update(deltaTime);
    computePalette(*_skeleton, _playable->outputPose(), _palette);
}

void HobiAnimator::evaluate(float deltaTime) { update(deltaTime); }

void HobiAnimator::evaluate() { update(0.0f); }

float HobiAnimator::speed() const noexcept {
    return _playable != nullptr ? _playable->speed() : 1.0f;
}

void HobiAnimator::setSpeed(float value) {
    if (_playable != nullptr) _playable->setSpeed(value);
}

bool HobiAnimator::isGraphPlaying() const noexcept {
    return _playable != nullptr && _playable->isGraphPlaying();
}

void HobiAnimator::pauseGraph() {
    if (_playable != nullptr) _playable->pauseGraph();
}

void HobiAnimator::unpauseGraph() {
    if (_playable != nullptr) _playable->unpauseGraph();
}

RootMotionDelta HobiAnimator::consumeRootMotion() const {
    return _playable != nullptr ? _playable->rootMotionDelta() : RootMotionDelta{};
}

HobiState* HobiAnimator::play(const Clip& clip) {
    return _playable != nullptr ? _playable->play(clip) : nullptr;
}

HobiState* HobiAnimator::play(const Clip& clip, float fadeDuration, FadeMode mode) {
    return _playable != nullptr ? _playable->play(clip, fadeDuration, mode) : nullptr;
}

HobiState* HobiAnimator::play(ITransition& transition) {
    return _playable != nullptr ? _playable->play(transition) : nullptr;
}

HobiState* HobiAnimator::play(ITransition& transition, float fadeDuration,
                                        FadeMode mode) {
    return _playable != nullptr ? _playable->play(transition, fadeDuration, mode) : nullptr;
}

HobiState* HobiAnimator::play(HobiState* state) {
    if (_playable == nullptr || state == nullptr) return nullptr;
    return _playable->play(state);
}

HobiState* HobiAnimator::play(HobiState* state, float fadeDuration,
                                        FadeMode mode) {
    if (_playable == nullptr || state == nullptr) return nullptr;
    return _playable->play(state, fadeDuration, mode);
}

HobiState* HobiAnimator::tryPlay(Key key) {
    return _playable != nullptr ? _playable->tryPlay(key) : nullptr;
}

HobiState* HobiAnimator::tryPlay(Key key, float fadeDuration, FadeMode mode) {
    if (_playable == nullptr) return nullptr;

    HobiState* state = nullptr;
    if (!_playable->states().tryGet(key, state)) return nullptr;
    return _playable->play(state, fadeDuration, mode);
}

void HobiAnimator::stop() {
    if (_playable != nullptr) _playable->stop();
}

HobiState* HobiAnimator::stop(const Clip& clip) {
    return stop(Key::fromPointer(&clip));
}

HobiState* HobiAnimator::stop(Key key) {
    if (_playable == nullptr) return nullptr;

    HobiState* state = _playable->states().get(key);
    if (state != nullptr) state->stop();
    return state;
}

bool HobiAnimator::isPlaying() const {
    return _playable != nullptr && _playable->isPlaying();
}

bool HobiAnimator::isPlaying(const Clip& clip) const {
    return _playable != nullptr && _playable->isPlaying(clip);
}

bool HobiAnimator::isPlaying(Key key) const {
    if (_playable == nullptr) return false;

    HobiState* state = nullptr;
    return _playable->states().tryGet(key, state) && state->isPlaying();
}

bool HobiAnimator::isPlayingClip(const Clip& clip) const {
    return _playable != nullptr && _playable->isPlaying(clip);
}

HobiLayer* HobiAnimator::layer(int index) {
    return _playable != nullptr ? _playable->layers().getLayer(index) : nullptr;
}

void HobiAnimator::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    if (_playable != nullptr) _playable->gatherAnimationClips(clips);
}

}  // namespace anim
