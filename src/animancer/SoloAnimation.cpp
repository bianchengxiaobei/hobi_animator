#include "anim/animancer/SoloAnimation.h"

#include <algorithm>
#include <cmath>

#include "anim/clip/Clip.h"
#include "anim/skeleton/Palette.h"
#include "anim/skeleton/Skeleton.h"

namespace anim {

SoloAnimation::SoloAnimation(Skeleton& skeleton, const HumanoidAvatar* avatar) {
    initialize(skeleton, avatar);
}

void SoloAnimation::initialize(Skeleton& skeleton, const HumanoidAvatar* avatar) {
    destroy();

    _skeleton = &skeleton;
    _avatar = avatar;
    _pose.resizeLike(skeleton);
    _pose.resetToRest(skeleton);
    _palette.assign(static_cast<std::size_t>(skeleton.boneCount()), Mat4(1.0f));
}

void SoloAnimation::destroy() {
    _skeleton = nullptr;
    _avatar = nullptr;
    _clip = nullptr;
    _pose.clear();
    _palette.clear();
    _time = 0.0f;
    _isPlaying = false;
}

bool SoloAnimation::play(const Clip& clip) {
    if (_skeleton == nullptr) return false;
    if (clip.type == ClipType::HumanoidMuscle && _avatar == nullptr) return false;

    _clip = &clip;
    _time = 0.0f;
    _isPlaying = true;
    return true;
}

void SoloAnimation::stop() {
    _isPlaying = false;
    _time = 0.0f;
}

void SoloAnimation::setIsPlaying(bool value) {
    if (value && (_skeleton == nullptr || _clip == nullptr)) return;
    _isPlaying = value;
}

void SoloAnimation::setSpeed(float value) {
    _speed = value;
    if (value == 0.0f) {
        _isPlaying = false;
    } else if (_skeleton != nullptr && _clip != nullptr) {
        _isPlaying = true;
    }
}

void SoloAnimation::setTime(float value) {
    _time = value;
    if (_skeleton != nullptr && _clip != nullptr) _isPlaying = true;
}

float SoloAnimation::normalizedTime() const noexcept {
    if (_clip == nullptr || _clip->length <= 0.0f) return 0.0f;
    return _time / _clip->length;
}

void SoloAnimation::setNormalizedTime(float value) {
    if (_clip == nullptr || _clip->length <= 0.0f) {
        setTime(0.0f);
        return;
    }
    setTime(value * _clip->length);
}

void SoloAnimation::update(float deltaTime) {
    if (!_isPlaying || _skeleton == nullptr || _clip == nullptr) return;
    if (!std::isfinite(deltaTime)) return;

    const float length = _clip->length;
    if (length <= 0.0f) {
        _time = 0.0f;
        sampleAt(0.0f);
        if (!_clip->looping) _isPlaying = false;
        return;
    }

    const float nextTime = _time + deltaTime * _speed;
    float sampleTime = 0.0f;

    if (_clip->looping) {
        sampleTime = std::fmod(nextTime, length);
        if (sampleTime < 0.0f) sampleTime += length;
        _time = nextTime;
    } else {
        _time = std::clamp(nextTime, 0.0f, length);
        sampleTime = _time;
        if (_speed > 0.0f && _time >= length) {
            _isPlaying = false;
        } else if (_speed < 0.0f && _time <= 0.0f) {
            _isPlaying = false;
        }
    }

    sampleAt(sampleTime);
}

void SoloAnimation::sampleAt(float sampleTime) {
    sampleClip(*_clip, sampleTime, *_skeleton, _avatar, _pose);
    computePalette(*_skeleton, _pose, _palette);
}

void SoloAnimation::gatherAnimationClips(std::vector<const Clip*>& clips) const {
    if (_clip != nullptr) clips.push_back(_clip);
}

}  // namespace anim
