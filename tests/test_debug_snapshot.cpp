#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "anim/clip/Clip.h"
#include "anim/debug/NameResolver.h"
#include "anim/graph/Nodes.h"
#include "anim/graph/PlayableGraph.h"
#include "anim/debug/Snapshot.h"
#include "anim/hobi/Event.h"
#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/LayerList.h"
#include "anim/integration/Json.h"
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

class TestResolver final : public debug::NameResolver {
public:
    const Clip* a = nullptr;
    const Clip* b = nullptr;

    std::string_view boneName(int) const override { return {}; }

    std::string_view clipName(const Clip& clip) const override {
        if (&clip == a) return "A";
        if (&clip == b) return "B";
        return {};
    }
};

}

TEST(DebugSnapshot, CapturesGraphLayersStatesAndNodes) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clipA = makeClip(1.0f);
    BoneLocalClip clipB = makeClip(2.0f);
    TestResolver resolver;
    resolver.a = &clipA;
    resolver.b = &clipB;

    HobiLayer* layer = root.layers().getLayer(0);
    ASSERT_NE(layer, nullptr);
    HobiState* a = layer->play(clipA);
    ASSERT_NE(a, nullptr);
    HobiState* b = layer->play(clipB, 0.5f, FadeMode::FixedDuration);
    ASSERT_NE(b, nullptr);

    EventSequence events;
    events.add(0.5f, [] {});
    b->setEvents(&events);

    root.evaluate(0.25f);

    const debug::Snapshot snapshot = debug::capture(root, &resolver);

    ASSERT_EQ(snapshot.version, 1);
    EXPECT_EQ(snapshot.frameId, root.frameId());
    EXPECT_FLOAT_EQ(snapshot.deltaTime, 0.25f);
    EXPECT_TRUE(snapshot.graph.compiled);
    EXPECT_TRUE(snapshot.graph.playing);
    EXPECT_FALSE(snapshot.graph.dirty);
    EXPECT_FLOAT_EQ(snapshot.graph.speed, 1.0f);
    EXPECT_GT(snapshot.graph.nodeCount, 0u);
    EXPECT_TRUE(snapshot.graph.lastError.empty());

    ASSERT_EQ(snapshot.layers.size(), 1u);
    EXPECT_EQ(snapshot.layers[0].index, 0);
    EXPECT_EQ(snapshot.layers[0].currentState, 1);
    EXPECT_EQ(snapshot.layers[0].states.size(), 2u);

    ASSERT_EQ(snapshot.states.size(), 2u);
    EXPECT_EQ(snapshot.states[0].clipName, "A");
    EXPECT_EQ(snapshot.states[1].clipName, "B");
    EXPECT_EQ(snapshot.states[1].type, "ClipState");
    EXPECT_NE(snapshot.states[1].clipHash, 0u);
    EXPECT_FLOAT_EQ(snapshot.states[1].length, 2.0f);
    EXPECT_FLOAT_EQ(snapshot.states[1].speed, 1.0f);
    EXPECT_FLOAT_EQ(snapshot.states[1].effectiveSpeed, 1.0f);
    EXPECT_NEAR(snapshot.states[1].weight, 0.5f, 1e-5f);
    EXPECT_NEAR(snapshot.states[1].effectiveWeight, 0.5f, 1e-5f);
    EXPECT_EQ(snapshot.states[1].eventCount, 1);

    EXPECT_FALSE(snapshot.nodes.empty());
    EXPECT_EQ(snapshot.rootMotion.hasMotion, false);
}

TEST(DebugSnapshot, PauseAndResolveOptionalNames) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clip = makeClip(1.0f);
    ASSERT_NE(root.play(clip), nullptr);

    debug::Snapshot snapshot;
    debug::captureInto(root, snapshot);
    ASSERT_EQ(snapshot.states.size(), 1u);
    EXPECT_TRUE(snapshot.states[0].clipName.empty());

    root.pauseGraph();
    debug::captureInto(root, snapshot);
    EXPECT_FALSE(snapshot.graph.playing);
    EXPECT_TRUE(snapshot.states[0].playing);

    root.unpauseGraph();
    debug::captureInto(root, snapshot);
    EXPECT_TRUE(snapshot.graph.playing);
}

TEST(DebugSnapshot, ExposesGraphCompileError) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    PlayableGraph& graph = root.graph();
    const PlayableHandle a = graph.create<MixerNode>();
    const PlayableHandle b = graph.create<MixerNode>();
    graph.connect(a, 0, b, 0);
    graph.connect(b, 0, a, 0);
    graph.setOutput(a);

    EXPECT_FALSE(graph.compile());

    const debug::Snapshot snapshot = debug::capture(root);
    EXPECT_FALSE(snapshot.graph.compiled);
    EXPECT_FALSE(snapshot.graph.lastError.empty());
    EXPECT_NE(snapshot.graph.lastError.find("cycle"), std::string::npos);
}

TEST(DebugSnapshot, CaptureIntoReusesLayerAndStateCapacity) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clipA = makeClip(1.0f);
    BoneLocalClip clipB = makeClip(1.0f);
    HobiLayer* layer = root.layers().getLayer(0);
    ASSERT_NE(layer, nullptr);
    ASSERT_NE(layer->play(clipA), nullptr);
    ASSERT_NE(layer->play(clipB), nullptr);

    debug::Snapshot snapshot;
    debug::captureInto(root, snapshot);
    const auto layerCapacity = snapshot.layers.capacity();
    const auto stateCapacity = snapshot.states.capacity();
    const auto nodeCapacity = snapshot.nodes.capacity();

    for (int i = 0; i < 4; ++i) {
        debug::captureInto(root, snapshot);
    }

    EXPECT_EQ(snapshot.layers.capacity(), layerCapacity);
    EXPECT_EQ(snapshot.states.capacity(), stateCapacity);
    EXPECT_EQ(snapshot.nodes.capacity(), nodeCapacity);
}

TEST(DebugSnapshot, ClipHashIsContentStable) {
    Skeleton skeleton = makeSkeleton();
    BoneLocalClip clipA = makeClip(1.0f);
    BoneLocalClip clipB = makeClip(1.0f);
    BoneLocalClip clipC = makeClip(2.0f);

    HobiPlayable rootA;
    rootA.createOutput(skeleton);
    ASSERT_NE(rootA.play(clipA), nullptr);
    const debug::Snapshot snapshotA = debug::capture(rootA);

    HobiPlayable rootB;
    rootB.createOutput(skeleton);
    ASSERT_NE(rootB.play(clipB), nullptr);
    const debug::Snapshot snapshotB = debug::capture(rootB);

    HobiPlayable rootC;
    rootC.createOutput(skeleton);
    ASSERT_NE(rootC.play(clipC), nullptr);
    const debug::Snapshot snapshotC = debug::capture(rootC);

    ASSERT_EQ(snapshotA.states.size(), 1u);
    ASSERT_EQ(snapshotB.states.size(), 1u);
    ASSERT_EQ(snapshotC.states.size(), 1u);
    EXPECT_NE(snapshotA.states[0].clipHash, 0u);
    EXPECT_EQ(snapshotA.states[0].clipHash, snapshotB.states[0].clipHash);
    EXPECT_NE(snapshotA.states[0].clipHash, snapshotC.states[0].clipHash);
}

TEST(DebugSnapshot, JsonIsParseableAndStable) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clip = makeClip(1.0f);
    ASSERT_NE(root.play(clip), nullptr);

    const debug::Snapshot snapshot = debug::capture(root);
    const std::string text = debug::toJson(snapshot);
    EXPECT_EQ(text, debug::toJson(snapshot));

    json::Value value;
    std::string error;
    ASSERT_TRUE(json::parse(text, value, error)) << error;
    ASSERT_TRUE(value.isObject());

    const json::Value* version = value.find("version");
    ASSERT_NE(version, nullptr);
    EXPECT_TRUE(version->isNumber());
    EXPECT_EQ(version->number, 1.0);

    const json::Value* graph = value.find("graph");
    ASSERT_NE(graph, nullptr);
    ASSERT_TRUE(graph->isObject());
    EXPECT_TRUE(graph->find("compiled")->isBool());
    EXPECT_TRUE(graph->find("playing")->isBool());

    const json::Value* layers = value.find("layers");
    ASSERT_NE(layers, nullptr);
    ASSERT_TRUE(layers->isArray());
    ASSERT_EQ(layers->array.size(), 1u);
    EXPECT_TRUE(layers->at(0)->find("states")->isArray());

    const json::Value* states = value.find("states");
    ASSERT_NE(states, nullptr);
    ASSERT_TRUE(states->isArray());
    ASSERT_EQ(states->array.size(), 1u);
    EXPECT_TRUE(states->at(0)->find("clipName")->isString());

    const json::Value* rootMotion = value.find("rootMotion");
    ASSERT_NE(rootMotion, nullptr);
    EXPECT_TRUE(rootMotion->find("deltaPosition")->isArray());
    EXPECT_TRUE(rootMotion->find("deltaRotation")->isArray());
}
