#include "anim/animancer/MixerTransitions.h"

#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/MixerStates.h"
#include "anim/core/Assert.h"

namespace anim {

AnimancerState* ManualMixerTransition::createState(AnimancerPlayable& root) {
    ManualMixerState* mixer = createMixer(root);
    if (mixer == nullptr) return nullptr;

    initializeState(*mixer);
    mixer->setSpeed(speedValue);
    return mixer;
}

void ManualMixerTransition::apply(AnimancerState& state) {
    state.setSpeed(speedValue);
    state.setEvents(&eventSequence);
}

void ManualMixerTransition::gatherAnimationClips(std::vector<const Clip*>& output) const {
    for (const Clip* clip : clips) {
        gatherClip(output, clip);
    }
}

ManualMixerState* ManualMixerTransition::createMixer(AnimancerPlayable& root) const {
    return new ManualMixerState(&root);
}

void ManualMixerTransition::initializeState(ManualMixerState& mixer) const {
    mixer.addRange(clips);

    const int count = static_cast<int>(speeds.size());
    for (int i = 0; i < count && i < mixer.childCount(); ++i) {
        mixer.getChild(i)->setSpeed(speeds[static_cast<std::size_t>(i)]);
    }

    mixer.initializeSynchronizedChildren(synchronizeChildren);
}

// ---------------------------------------------------------------------------
// LinearMixerTransition
// ---------------------------------------------------------------------------

ManualMixerState* LinearMixerTransition::createMixer(AnimancerPlayable& root) const {
    return new LinearMixerState(&root);
}

void LinearMixerTransition::initializeState(ManualMixerState& mixer) const {
    ManualMixerTransition::initializeState(mixer);

    auto& linear = static_cast<LinearMixerState&>(mixer);

    if (!thresholds.empty()) {
        ANIM_CHECK_MSG(static_cast<int>(thresholds.size()) >= linear.childCount(),
                       "Threshold count must not be less than child count");
        linear.setThresholds(thresholds);
    }

    linear.setExtrapolateSpeed(extrapolateSpeed);
    linear.setParameter(defaultParameter);
}

// ---------------------------------------------------------------------------
// MixerTransition2D
// ---------------------------------------------------------------------------

ManualMixerState* MixerTransition2D::createMixer(AnimancerPlayable& root) const {
    switch (type) {
        case Type::Cartesian: return new CartesianMixerState(&root);
        case Type::Directional: return new DirectionalMixerState(&root);
    }
    return nullptr;
}

void MixerTransition2D::initializeState(ManualMixerState& mixer) const {
    ManualMixerTransition::initializeState(mixer);

    auto& mixer2D = static_cast<MixerState<Vec2>&>(mixer);

    if (!thresholds.empty()) {
        ANIM_CHECK_MSG(static_cast<int>(thresholds.size()) >= mixer2D.childCount(),
                       "Threshold count must not be less than child count");
        mixer2D.setThresholds(thresholds);
    }

    mixer2D.setParameter(defaultParameter);
}

}  // namespace anim
