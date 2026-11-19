#pragma once

#include <vector>

#include "anim/hobi/AnimationClipCollection.h"
#include "anim/core/Math.h"
#include "anim/skeleton/Pose.h"

namespace anim {

class Skeleton;
struct Clip;
struct HumanoidAvatar;

class SoloAnimation final : public IAnimationClipCollection {
public:
    SoloAnimation() = default;
    explicit SoloAnimation(Skeleton& skeleton, const HumanoidAvatar* avatar = nullptr);

    void initialize(Skeleton& skeleton, const HumanoidAvatar* avatar = nullptr);
    void destroy();

    bool isInitialized() const noexcept { return _skeleton != nullptr; }

    bool play(const Clip& clip);
    void stop();
    void update(float deltaTime);

    bool isPlaying() const noexcept { return _isPlaying; }
    void setIsPlaying(bool value);

    float speed() const noexcept { return _speed; }
    void setSpeed(float value);

    float time() const noexcept { return _time; }
    void setTime(float value);

    float normalizedTime() const noexcept;
    void setNormalizedTime(float value);

    const Clip* clip() const noexcept { return _clip; }
    Skeleton* skeleton() const noexcept { return _skeleton; }
    const HumanoidAvatar* avatar() const noexcept { return _avatar; }

    const Pose& pose() const noexcept { return _pose; }
    const std::vector<Mat4>& palette() const noexcept { return _palette; }

    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

private:
    void sampleAt(float sampleTime);

    Skeleton* _skeleton = nullptr;
    const HumanoidAvatar* _avatar = nullptr;
    const Clip* _clip = nullptr;
    Pose _pose;
    std::vector<Mat4> _palette;
    float _time = 0.0f;
    float _speed = 1.0f;
    bool _isPlaying = false;
};

}  // namespace anim
