#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "anim/hobi/HobiAnimator.h"
#include "anim/hobi/AnimationClipCollection.h"

namespace anim {

struct Clip;

struct NamedAnimation {
    std::string name;
    const Clip* clip = nullptr;
};

class NamedHobiAnimator final : public IAnimationClipCollection {
public:
    void initialize(Skeleton& skeleton, const HumanoidAvatar* avatar = nullptr,
                    int layerCount = 1);
    void destroy();

    bool isInitialized() const noexcept { return _animator.isInitialized(); }
    void update(float deltaTime) { _animator.update(deltaTime); }

    HobiAnimator& animator() noexcept { return _animator; }
    const HobiAnimator& animator() const noexcept { return _animator; }
    const std::vector<Mat4>& palette() const noexcept { return _animator.palette(); }

    void setAnimations(std::initializer_list<NamedAnimation> animations);
    void setAnimations(const std::vector<NamedAnimation>& animations);
    bool addAnimation(std::string name, const Clip& clip);
    bool removeAnimation(std::string_view name);

    const Clip* findAnimation(std::string_view name) const;
    const std::vector<std::string>& animationNames() const noexcept { return _order; }

    bool playAutomatically() const noexcept { return _playAutomatically; }
    void setPlayAutomatically(bool value) noexcept { _playAutomatically = value; }

    const Clip* defaultAnimation() const;
    bool setDefaultAnimation(std::string_view name);
    HobiState* playDefault();
    HobiState* start();

    HobiState* play(std::string_view name);
    HobiState* play(std::string_view name, float fadeDuration,
                         FadeMode mode = FadeMode::FixedSpeed);
    HobiState* stop(std::string_view name);
    void stop();
    bool isPlaying(std::string_view name) const;

    void gatherAnimationClips(std::vector<const Clip*>& clips) const override;

private:
    void createStates();

    HobiAnimator _animator;
    std::unordered_map<std::string, const Clip*> _byName;
    std::vector<std::string> _order;
    std::string _defaultName;
    bool _playAutomatically = true;
};

}  // namespace anim
