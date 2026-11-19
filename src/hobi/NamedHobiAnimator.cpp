#include "anim/hobi/NamedHobiAnimator.h"

#include <algorithm>

#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/StateDictionary.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {

void NamedHobiAnimator::initialize(Skeleton& skeleton, const HumanoidAvatar* avatar,
                                        int layerCount) {
    _animator.initialize(skeleton, avatar, layerCount);
    createStates();
}

void NamedHobiAnimator::destroy() { _animator.destroy(); }

void NamedHobiAnimator::setAnimations(
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

void NamedHobiAnimator::setAnimations(const std::vector<NamedAnimation>& animations) {
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

bool NamedHobiAnimator::addAnimation(std::string name, const Clip& clip) {
    if (name.empty()) return false;

    if (_byName.find(name) == _byName.end()) _order.push_back(name);
    _byName[name] = &clip;
    if (_defaultName.empty()) _defaultName = name;

    if (_animator.isInitialized()) {
        _animator.playable().states().getOrCreate(clip);
    }
    return true;
}

bool NamedHobiAnimator::removeAnimation(std::string_view name) {
    const auto it = _byName.find(std::string(name));
    if (it == _byName.end()) return false;

    _byName.erase(it);
    _order.erase(std::remove(_order.begin(), _order.end(), std::string(name)), _order.end());

    if (!_defaultName.empty() && _defaultName == std::string(name)) {
        _defaultName = _order.empty() ? std::string() : _order.front();
    }
    return true;
}

const Clip* NamedHobiAnimator::findAnimation(std::string_view name) const {
    const auto it = _byName.find(std::string(name));
    return it == _byName.end() ? nullptr : it->second;
}

const Clip* NamedHobiAnimator::defaultAnimation() const {
    return _defaultName.empty() ? nullptr : findAnimation(_defaultName);
}

bool NamedHobiAnimator::setDefaultAnimation(std::string_view name) {
    if (findAnimation(name) == nullptr) return false;
    _defaultName = std::string(name);
    return true;
}

HobiState* NamedHobiAnimator::playDefault() {
    const Clip* clip = defaultAnimation();
    return clip != nullptr ? _animator.play(*clip) : nullptr;
}

HobiState* NamedHobiAnimator::start() {
    return _playAutomatically ? playDefault() : nullptr;
}

HobiState* NamedHobiAnimator::play(std::string_view name) {
    const Clip* clip = findAnimation(name);
    return clip != nullptr ? _animator.play(*clip) : nullptr;
}

HobiState* NamedHobiAnimator::play(std::string_view name, float fadeDuration,
                                             FadeMode mode) {
    const Clip* clip = findAnimation(name);
    return clip != nullptr ? _animator.play(*clip, fadeDuration, mode) : nullptr;
}

HobiState* NamedHobiAnimator::stop(std::string_view name) {
    const Clip* clip = findAnimation(name);
    return clip != nullptr ? _animator.stop(*clip) : nullptr;
}

void NamedHobiAnimator::stop() { _animator.stop(); }

bool NamedHobiAnimator::isPlaying(std::string_view name) const {
    const Clip* clip = findAnimation(name);
    return clip != nullptr && _animator.isPlayingClip(*clip);
}

void NamedHobiAnimator::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    for (const std::string& name : _order) {
        const Clip* clip = findAnimation(name);
        gatherClip(clips, clip);
    }
}

void NamedHobiAnimator::createStates() {
    if (!_animator.isInitialized()) return;

    for (const std::string& name : _order) {
        const Clip* clip = findAnimation(name);
        if (clip != nullptr) _animator.playable().states().getOrCreate(*clip);
    }
}

}  // namespace anim
