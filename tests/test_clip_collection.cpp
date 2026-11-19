#include <gtest/gtest.h>

#include <vector>

#include "anim/animancer/AnimancerAnimator.h"
#include "anim/animancer/AnimancerLayer.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/ClipTransitionSequence.h"
#include "anim/animancer/LayerList.h"
#include "anim/animancer/MixerStates.h"
#include "anim/animancer/MixerTransitions.h"
#include "anim/animancer/StateDictionary.h"
#include "anim/animancer/Transition.h"
#include "anim/clip/Clip.h"
#include "anim/skeleton/Skeleton.h"

using namespace anim;

namespace {

Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Root"_sid, -1, Transform::identity());
    return skeleton;
}

BoneLocalClip makeClip(float length) {
    BoneLocalClip clip;
    clip.length = length;
    return clip;
}

bool contains(const std::vector<const Clip*>& clips, const Clip* clip) {
    for (const Clip* value : clips) {
        if (value == clip) return true;
    }
    return false;
}

}  // namespace

TEST(AnimationClipCollection, StateTreeGathersUniqueClips) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);

    AnimancerLayer* layer = root.layers().getLayer(0);
    auto* mixer = new ManualMixerState(&root);
    layer->addChild(mixer);

    mixer->add(clipA);
    auto* nested = new ManualMixerState(&root);
    mixer->add(nested);
    nested->add(clipB);
    nested->add(clipB);

    std::vector<const Clip*> clips;
    layer->gatherAnimationClips(clips);

    EXPECT_EQ(clips.size(), 2u);
    EXPECT_TRUE(contains(clips, &clipA));
    EXPECT_TRUE(contains(clips, &clipB));
}

TEST(AnimationClipCollection, PlayableGathersAcrossLayers) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);
    root.layers().setCount(2);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);

    root.layers().getLayer(0)->play(clipA);
    root.layers().getLayer(1)->play(clipB);

    std::vector<const Clip*> clips;
    root.gatherAnimationClips(clips);

    EXPECT_EQ(clips.size(), 2u);
    EXPECT_TRUE(contains(clips, &clipA));
    EXPECT_TRUE(contains(clips, &clipB));
}

TEST(AnimationClipCollection, StateDictionaryCurrentTracksLayerZero) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);
    AnimancerLayer* layer = root.layers().getLayer(0);

    EXPECT_EQ(root.states().current(), nullptr);

    AnimancerState* stateA = layer->play(clipA);
    EXPECT_EQ(root.states().current(), stateA);

    AnimancerState* stateB = layer->play(clipB);
    EXPECT_EQ(root.states().current(), stateB);

    std::vector<const Clip*> clips;
    root.states().gatherAnimationClips(clips);
    EXPECT_EQ(clips.size(), 2u);
    EXPECT_TRUE(contains(clips, &clipA));
    EXPECT_TRUE(contains(clips, &clipB));
}

TEST(AnimationClipCollection, CreateIfNewCreatesWithoutDuplicates) {
    Skeleton skeleton = makeSkeleton();
    AnimancerPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);
    const BoneLocalClip clipC = makeClip(1.0f);
    AnimancerLayer* layer = root.layers().getLayer(0);

    root.states().createIfNew({&clipA, &clipB, nullptr, &clipA});

    ASSERT_EQ(root.states().states().size(), 2u);
    AnimancerState* stateA = root.states().get(Key::fromPointer(&clipA));
    AnimancerState* stateB = root.states().get(Key::fromPointer(&clipB));
    ASSERT_NE(stateA, nullptr);
    ASSERT_NE(stateB, nullptr);
    EXPECT_EQ(stateA->parent(), nullptr);
    EXPECT_EQ(stateB->parent(), nullptr);

    const std::vector<const Clip*> requested = {&clipC, &clipA};
    root.states().createIfNew(requested);
    EXPECT_EQ(root.states().states().size(), 3u);
    EXPECT_NE(root.states().get(Key::fromPointer(&clipC)), nullptr);

    layer->createIfNew({&clipA, &clipB});
    EXPECT_EQ(layer->childCount(), 2);
    EXPECT_EQ(stateA->parent(), layer);
    EXPECT_EQ(stateB->parent(), layer);

    layer->createIfNew({&clipA, &clipB});
    EXPECT_EQ(layer->childCount(), 2);

    root.states().destroy(clipC);
    EXPECT_EQ(root.states().states().size(), 2u);
}

TEST(AnimationClipCollection, TransitionsGatherNestedClips) {
    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);
    const BoneLocalClip clipC = makeClip(1.0f);

    ClipTransition single;
    single.clip = &clipA;
    std::vector<const Clip*> singleClips;
    single.gatherAnimationClips(singleClips);
    EXPECT_EQ(singleClips.size(), 1u);
    EXPECT_TRUE(contains(singleClips, &clipA));

    ClipTransitionSequence sequence;
    sequence.clip = &clipA;
    ClipTransition second;
    second.clip = &clipB;
    sequence.others.push_back(second);
    second.clip = &clipA;
    sequence.others.push_back(second);

    std::vector<const Clip*> sequenceClips;
    sequence.gatherAnimationClips(sequenceClips);
    EXPECT_EQ(sequenceClips.size(), 2u);
    EXPECT_TRUE(contains(sequenceClips, &clipA));
    EXPECT_TRUE(contains(sequenceClips, &clipB));

    ITransition& transitionBase = sequence;
    std::vector<const Clip*> interfaceClips;
    transitionBase.gatherAnimationClips(interfaceClips);
    EXPECT_EQ(interfaceClips.size(), 2u);
    EXPECT_TRUE(contains(interfaceClips, &clipA));
    EXPECT_TRUE(contains(interfaceClips, &clipB));

    ManualMixerTransition mixer;
    mixer.clips = {&clipA, &clipC, &clipA};
    std::vector<const Clip*> mixerClips;
    mixer.gatherAnimationClips(mixerClips);
    EXPECT_EQ(mixerClips.size(), 2u);
    EXPECT_TRUE(contains(mixerClips, &clipA));
    EXPECT_TRUE(contains(mixerClips, &clipC));

    TransitionAsset<ClipTransition> asset;
    asset.transition.clip = &clipC;
    std::vector<const Clip*> assetClips;
    asset.gatherAnimationClips(assetClips);
    EXPECT_EQ(assetClips.size(), 1u);
    EXPECT_TRUE(contains(assetClips, &clipC));

    UnShared<ClipTransition> unshared;
    unshared.base().clip = &clipB;
    std::vector<const Clip*> unsharedClips;
    unshared.gatherAnimationClips(unsharedClips);
    EXPECT_EQ(unsharedClips.size(), 1u);
    EXPECT_TRUE(contains(unsharedClips, &clipB));
}

TEST(AnimationClipCollection, AnimatorForwardsToPlayable) {
    Skeleton skeleton = makeSkeleton();
    AnimancerAnimator animator;
    animator.initialize(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    animator.play(clip);

    std::vector<const Clip*> clips;
    animator.gatherAnimationClips(clips);

    EXPECT_EQ(clips.size(), 1u);
    EXPECT_TRUE(contains(clips, &clip));
}
