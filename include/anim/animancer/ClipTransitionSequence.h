#pragma once

#include <functional>
#include <vector>

#include "anim/animancer/Transition.h"

// 连续过渡（对应 Utilities/Transitions/ClipTransitionSequence.cs）。
namespace anim {

class AnimancerState;

class ClipTransitionSequence : public ClipTransition {
public:
    std::vector<ClipTransition> others;

    ClipTransition& lastTransition() noexcept;
    const ClipTransition& lastTransition() const noexcept;

    AnimancerEvent& endEvent() noexcept;
    const AnimancerEvent& endEvent() const noexcept;

    void apply(AnimancerState& state) override;

    bool isValid() const override;
    bool isLooping() const noexcept override;
    float length() const override;
    float maximumDuration() const override;
    float averageAngularSpeed() const override;
    Vec3 averageVelocity() const override;
    void gatherClips(std::vector<const Clip*>& clips) const override;
    void copyFrom(const ClipTransitionSequence& other);

    void addEvent(float time, bool normalized, std::function<void()> callback);

private:
    struct EndCallback {
        ClipTransitionSequence* owner = nullptr;
        AnimancerState* state = nullptr;
        int index = 0;

        void operator()() const;
    };

    static bool isOwnEnd(const std::function<void()>& callback,
                         const ClipTransitionSequence* owner);
    void installChain(AnimancerState* state);
    void playNext(AnimancerState* state, int index);
    static bool tryAddEvent(ClipTransition& transition, float length, float& time,
                            const std::function<void()>& callback);
    static void addEventTo(ClipTransition& transition, float time,
                           const std::function<void()>& callback);
};

}  // namespace anim
