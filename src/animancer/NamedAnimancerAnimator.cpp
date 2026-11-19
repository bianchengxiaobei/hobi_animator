#include "anim/animancer/NamedAnimancerAnimator.h"

#include <algorithm>

#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/StateDictionary.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {

void NamedAnimancerAnimator::initialize(Skeleton& skeleton, const HumanoidAvatar* avatar,
                                        int layerCount) {
    _animator.initialize(skeleton, avatar, layerCount);
    createStates();
}

void NamedAnimancerAnimator::destroy() { _animator.destroy(); }

void NamedAnimancerAnimator::setAnimations(
    std::initializer_list<NamedAnimation> animations) {
    _byName.clear();
    _order.clear();
    _defaultName.clear();

    for (const NamedAnimation& animation : animations) {
        if (animation.clip == nullptr || animation.name.empty()) continue;
        if (_byName.find(animation.name) == _byName.end()) _order.push_back(animation.name);
        _byName[animation.name] = animation.clip;
    }

    if (!_order.empty()) _defaultName = _order.front();
    createStates();
}

void NamedAnimancerAnimator::setAnimations(const std::vector<NamedAnimation>& animations) {
    _byName.clear();
    _order.clear();
    _defaultName.clear();

    for (const NamedAnimation& animation : animations) {
        if (animation.clip == nullptr || animation.name.empty()) continue;
        if (_byName.find(animation.name) == _byName.end()) _order.push_back(animation.name);
        _byName[animation.name] = animation.clip;
    }

    if (!_order.empty()) _defaultName = _order.front();
    createStates();
}

bool NamedAnimancerAnimator::addAnimation(std::string name, const Clip& clip) {
    if (name.empty()) return false;

    if (_byName.find(name) == _byName.end()) _order.push_back(name);
    _byName[name] = &clip;
    if (_defaultName.empty()) _defaultName = name;

    if (_animator.isInitialized()) {
        _animator.playable().states().getOrCreate(clip);
    }
    return true;
}

bool NamedAnimancerAnimator::removeAnimation(std::string_view name) {
    const auto it = _byName.find(std::string(name));
    if (it == _byName.end()) return false;

    _byName.erase(it);
    _order.erase(std::remove(_order.begin(), _order.end(), std::string(name)), _order.end());

    if (!_defaultName.empty() && _defaultName == std::string(name)) {
        _defaultName = _order.empty() ? std::string() : _order.front();
    }
    return true;
}

const Clip* NamedAnimancerAnimator::findAnimation(std::string_view name) const {
    const auto it = _byName.find(std::string(name));
    return it == _byName.end() ? nullptr : it->second;
}

const Clip* NamedAnimancerAnimator::defaultAnimation() const {
    return _defaultName.empty() ? nullptr : findAnimation(_defaultName);
}

bool NamedAnimancerAnimator::setDefaultAnimation(std::string_view name) {
    if (findAnimation(name) == nullptr) return false;
    _defaultName = std::string(name);
    return true;
}

AnimancerState* NamedAnimancerAnimator::playDefault() {
    const Clip* clip = defaultAnimation();
    return clip != nullptr ? _animator.play(*clip) : nullptr;
}

AnimancerState* NamedAnimancerAnimator::start() {
    return _playAutomatically ? playDefault() : nullptr;
}

AnimancerState* NamedAnimancerAnimator::play(std::string_view name) {
    const Clip* clip = findAnimation(name);
    return clip != nullptr ? _animator.play(*clip) : nullptr;
}

AnimancerState* NamedAnimancerAnimator::play(std::string_view name, float fadeDuration,
                                             FadeMode mode) {
    const Clip* clip = findAnimation(name);
    return clip != nullptr ? _animator.play(*clip, fadeDuration, mode) : nullptr;
}

AnimancerState* NamedAnimancerAnimator::stop(std::string_view name) {
    const Clip* clip = findAnimation(name);
    return clip != nullptr ? _animator.stop(*clip) : nullptr;
}

void NamedAnimancerAnimator::stop() { _animator.stop(); }

bool NamedAnimancerAnimator::isPlaying(std::string_view name) const {
    const Clip* clip = findAnimation(name);
    return clip != nullptr && _animator.isPlayingClip(*clip);
}

void NamedAnimancerAnimator::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    for (const std::string& name : _order) {
        const Clip* clip = findAnimation(name);
        gatherClip(clips, clip);
    }
}

void NamedAnimancerAnimator::createStates() {
    if (!_animator.isInitialized()) return;

    for (const std::string& name : _order) {
        const Clip* clip = findAnimation(name);
        if (clip != nullptr) _animator.playable().states().getOrCreate(*clip);
    }
}

}  // namespace anim
