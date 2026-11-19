#include <gtest/gtest.h>

#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/ClipState.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/StateDictionary.h"
#include "anim/hobi/Transition.h"
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

}  // namespace

TEST(HobiLayer, ImmediatePlayStopsOthers) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);

    HobiLayer* layer = root.layers().getLayer(0);
    HobiState* a = layer->play(clipA);
    HobiState* b = layer->play(clipB);

    EXPECT_EQ(layer->currentState(), b);
    EXPECT_TRUE(b->isPlaying());
    EXPECT_FLOAT_EQ(b->weight(), 1.0f);
    EXPECT_FALSE(a->isPlaying());
    EXPECT_FLOAT_EQ(a->weight(), 0.0f);
    EXPECT_GE(layer->commandCount(), 2);
}

TEST(HobiLayer, GetOrCreateStateReusesClipState) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    HobiLayer* layer = root.layers().getLayer(0);

    HobiState* first = layer->getOrCreateState(clip);
    HobiState* second = layer->getOrCreateState(clip);

    EXPECT_EQ(first, second);
    EXPECT_EQ(root.states().get(Key::fromPointer(&clip)), first);
}

TEST(HobiLayer, GetOrCreateStateChainsAcrossLayers) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    HobiLayer* layer0 = root.layers().getLayer(0);
    HobiState* first = layer0->getOrCreateState(clip);
    ASSERT_NE(first, nullptr);

    root.layers().setCount(2);
    HobiLayer* layer1 = root.layers().getLayer(1);
    ASSERT_NE(layer1, nullptr);
    HobiState* second = layer1->getOrCreateState(clip);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second, first);
    EXPECT_EQ(second->parent(), layer1);
    EXPECT_EQ(second->key(), Key::fromPointer(first));
    EXPECT_EQ(root.states().get(Key::fromPointer(&clip)), first);
    EXPECT_EQ(root.states().get(Key::fromPointer(first)), second);
    EXPECT_EQ(layer1->getOrCreateState(clip), second);

    root.layers().setCount(3);
    HobiLayer* layer2 = root.layers().getLayer(2);
    ASSERT_NE(layer2, nullptr);
    HobiState* third = layer2->getOrCreateState(clip);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third->parent(), layer2);
    EXPECT_EQ(third->key(), Key::fromPointer(second));
    EXPECT_EQ(root.states().get(Key::fromPointer(second)), third);
}

TEST(HobiLayer, TransitionGetOrCreateChainsAcrossLayers) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    HobiLayer* layer0 = root.layers().getLayer(0);
    HobiState* first = layer0->getOrCreateState(clip);
    ASSERT_NE(first, nullptr);

    root.layers().setCount(2);
    HobiLayer* layer1 = root.layers().getLayer(1);
    ASSERT_NE(layer1, nullptr);

    ClipTransition transition;
    transition.clip = &clip;
    HobiState* second = layer1->getOrCreateState(transition);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second, first);
    EXPECT_EQ(second->parent(), layer1);
    EXPECT_EQ(second->key(), Key::fromPointer(first));
    EXPECT_EQ(root.states().get(Key::fromPointer(first)), second);
    EXPECT_EQ(layer1->getOrCreateState(transition), second);
}

TEST(HobiLayer, GetOrCreateStateCanSetClip) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clipA = makeClip(1.0f);
    const BoneLocalClip clipB = makeClip(1.0f);
    HobiLayer* layer = root.layers().getLayer(0);

    HobiState* state = layer->createState(clipA);
    ASSERT_NE(state, nullptr);
    state->setKey(Key::fromPointer(&clipB));
    root.states().registerState(state);

    EXPECT_EQ(layer->getOrCreateState(clipB, true), state);
    EXPECT_EQ(state->clip(), &clipB);
}

TEST(HobiLayer, FromStartClonesWeightlessState) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    HobiLayer* layer = root.layers().getLayer(0);

    HobiState* original = layer->play(clip);
    original->setTimeD(0.5);

    HobiState* clone = layer->play(clip, 0.25f, FadeMode::FromStart);

    EXPECT_NE(clone, original);
    EXPECT_EQ(layer->childCount(), 2);
    EXPECT_FLOAT_EQ(clone->weight(), 0.0f);
    EXPECT_DOUBLE_EQ(clone->timeD(), 0.0);
    EXPECT_EQ(layer->currentState(), clone);
}

TEST(HobiLayer, LayerListGrowsAndShrinks) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    EXPECT_EQ(root.layers().count(), 1);

    root.layers().setCount(3);
    EXPECT_EQ(root.layers().count(), 3);
    EXPECT_NE(root.layers().getLayer(2), nullptr);

    root.layers().setCount(1);
    EXPECT_EQ(root.layers().count(), 1);
    EXPECT_EQ(root.layers().getLayer(1), nullptr);
}

TEST(HobiLayer, StateDictionaryCreatesClipState) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    const BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.states().getOrCreate(clip);

    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->clip(), &clip);
    EXPECT_EQ(root.states().getOrCreate(clip), state);
}
