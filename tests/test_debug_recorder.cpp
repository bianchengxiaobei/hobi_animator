#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "anim/clip/Clip.h"
#include "anim/core/Hash.h"
#include "anim/debug/Recorder.h"
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

BoneLocalClip makeClip(float length, bool looping = false) {
    BoneLocalClip clip;
    clip.length = length;
    clip.looping = looping;
    return clip;
}

debug::RecorderConfig makeConfig(int frames, int events) {
    debug::RecorderConfig config;
    config.frameCapacity = frames;
    config.eventCapacity = events;
    return config;
}

}

TEST(DebugRecorder, RecordsFramesAndForwardEvents) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    EventSequence events;
    events.add(0.5f, [] {}, "Mid");
    events.endEvent() = HobiEvent(1.0f, [] {}, "End");
    state->setEvents(&events);

    debug::Recorder recorder;
    recorder.attach(root);
    EXPECT_EQ(root.eventObserver(), &recorder);

    root.evaluate(0.6f);
    recorder.recordFrame();

    ASSERT_EQ(recorder.frameCount(), 1);
    const debug::FrameRecord* firstFrame = recorder.frameAt(0);
    ASSERT_NE(firstFrame, nullptr);
    EXPECT_EQ(firstFrame->frameId, root.frameId());
    EXPECT_FLOAT_EQ(firstFrame->deltaTime, 0.6f);
    EXPECT_FLOAT_EQ(firstFrame->graphSpeed, 1.0f);
    EXPECT_TRUE(firstFrame->graphPlaying);
    ASSERT_EQ(firstFrame->layers.size(), 1u);
    EXPECT_EQ(firstFrame->layers[0].index, 0);
    EXPECT_EQ(firstFrame->layers[0].currentState, 0);
    ASSERT_EQ(firstFrame->states.size(), 1u);
    EXPECT_EQ(firstFrame->states[0].layer, 0);
    EXPECT_EQ(firstFrame->states[0].indexInLayer, 0);
    EXPECT_EQ(firstFrame->states[0].playable, state->playable());
    EXPECT_NEAR(firstFrame->states[0].time, 0.6, 1e-6);
    EXPECT_FLOAT_EQ(firstFrame->states[0].weight, 1.0f);
    EXPECT_TRUE(firstFrame->states[0].playing);

    ASSERT_EQ(recorder.eventCount(), 1);
    const debug::EventRecord* mid = recorder.eventAt(0);
    ASSERT_NE(mid, nullptr);
    EXPECT_EQ(mid->frameId, firstFrame->frameId);
    EXPECT_EQ(mid->statePlayable, state->playable());
    EXPECT_EQ(mid->layer, 0);
    EXPECT_EQ(mid->stateIndex, 0);
    EXPECT_EQ(mid->eventIndex, 0);
    EXPECT_EQ(mid->kind, EventKind::Normal);
    EXPECT_FLOAT_EQ(mid->normalizedTime, 0.5f);
    EXPECT_TRUE(mid->playingForwards);
    EXPECT_EQ(mid->eventNameHash, hashString("Mid"));

    root.evaluate(0.5f);
    recorder.recordFrame();

    ASSERT_EQ(recorder.frameCount(), 2);
    ASSERT_EQ(recorder.eventCount(), 2);
    const debug::FrameRecord* secondFrame = recorder.frameAt(0);
    ASSERT_NE(secondFrame, nullptr);
    EXPECT_GT(secondFrame->frameId, firstFrame->frameId);
    EXPECT_EQ(recorder.frameAt(-1), firstFrame);
    EXPECT_EQ(recorder.frameAt(1), nullptr);
    EXPECT_EQ(recorder.frameAt(-2), nullptr);

    const debug::EventRecord* end = recorder.eventAt(0);
    ASSERT_NE(end, nullptr);
    EXPECT_EQ(end->frameId, secondFrame->frameId);
    EXPECT_EQ(end->kind, EventKind::End);
    EXPECT_EQ(end->eventIndex, -1);
    EXPECT_FLOAT_EQ(end->normalizedTime, 1.0f);
    EXPECT_TRUE(end->playingForwards);
    EXPECT_EQ(end->eventNameHash, hashString("End"));
    ASSERT_NE(recorder.eventAt(-1), nullptr);
    EXPECT_EQ(recorder.eventAt(-1)->kind, EventKind::Normal);
    EXPECT_EQ(recorder.eventAt(1), nullptr);
}

TEST(DebugRecorder, RecordsBackwardsAndLoopingEvents) {
    Skeleton skeleton = makeSkeleton();

    HobiPlayable root;
    root.createOutput(skeleton);
    BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    EventSequence events;
    events.add(0.5f, [] {});
    state->setEvents(&events);
    state->setTimeD(1.0);
    state->setSpeed(-1.0f);

    debug::Recorder recorder;
    recorder.attach(root);
    root.evaluate(0.6f);
    recorder.recordFrame();

    ASSERT_EQ(recorder.eventCount(), 1);
    EXPECT_EQ(recorder.eventAt(0)->eventIndex, 0);
    EXPECT_EQ(recorder.eventAt(0)->kind, EventKind::Normal);
    EXPECT_FALSE(recorder.eventAt(0)->playingForwards);
    ASSERT_EQ(recorder.frameCount(), 1);
    EXPECT_NEAR(recorder.frameAt(0)->states[0].time, 0.4, 1e-6);

    HobiPlayable loopRoot;
    loopRoot.createOutput(skeleton);
    BoneLocalClip loopClip = makeClip(1.0f, true);
    HobiState* loopState = loopRoot.play(loopClip);
    ASSERT_NE(loopState, nullptr);

    EventSequence loopEvents;
    loopEvents.add(0.25f, [] {});
    loopState->setEvents(&loopEvents);

    debug::Recorder loopRecorder;
    loopRecorder.attach(loopRoot);
    loopRoot.evaluate(0.3f);
    loopRecorder.recordFrame();
    loopRoot.evaluate(1.0f);
    loopRecorder.recordFrame();

    ASSERT_GE(loopRecorder.eventCount(), 2);
    for (int offset = 0; offset < loopRecorder.eventCount(); ++offset) {
        const debug::EventRecord* event = loopRecorder.eventAt(-offset);
        ASSERT_NE(event, nullptr);
        EXPECT_EQ(event->eventIndex, 0);
        EXPECT_EQ(event->kind, EventKind::Normal);
        EXPECT_TRUE(event->playingForwards);
    }
    ASSERT_GE(loopRecorder.frameCount(), 2);
    EXPECT_GT(loopRecorder.eventAt(0)->frameId, loopRecorder.eventAt(-1)->frameId);
}

TEST(DebugRecorder, RingCapacityClearAndLookup) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    EventSequence events;
    events.endEvent() = HobiEvent(1.0f, [] {}, "End");
    state->setEvents(&events);
    state->setTimeD(1.0);

    debug::Recorder recorder(makeConfig(2, 2));
    recorder.attach(root);
    for (int i = 0; i < 3; ++i) {
        root.evaluate(0.1f);
        recorder.recordFrame();
    }

    EXPECT_EQ(recorder.frameCount(), 2);
    EXPECT_EQ(recorder.eventCount(), 2);
    ASSERT_NE(recorder.frameAt(0), nullptr);
    ASSERT_NE(recorder.frameAt(-1), nullptr);
    EXPECT_EQ(recorder.frameAt(0)->frameId, 3u);
    EXPECT_EQ(recorder.frameAt(-1)->frameId, 2u);
    EXPECT_EQ(recorder.frameAt(-2), nullptr);
    EXPECT_EQ(recorder.frameAt(1), nullptr);
    EXPECT_EQ(recorder.eventAt(0)->kind, EventKind::End);
    EXPECT_EQ(recorder.eventAt(0)->frameId, 3u);
    EXPECT_EQ(recorder.eventAt(-1)->frameId, 2u);
    EXPECT_EQ(recorder.eventAt(-2), nullptr);
    EXPECT_EQ(recorder.eventAt(1), nullptr);

    const auto stateCapacity = recorder.frameAt(0)->states.capacity();
    const auto layerCapacity = recorder.frameAt(0)->layers.capacity();
    recorder.clear();
    EXPECT_EQ(recorder.frameCount(), 0);
    EXPECT_EQ(recorder.eventCount(), 0);
    EXPECT_EQ(recorder.frameAt(0), nullptr);
    EXPECT_EQ(recorder.eventAt(0), nullptr);

    root.evaluate(0.1f);
    recorder.recordFrame();
    ASSERT_EQ(recorder.frameCount(), 1);
    EXPECT_EQ(recorder.frameAt(0)->states.capacity(), stateCapacity);
    EXPECT_EQ(recorder.frameAt(0)->layers.capacity(), layerCapacity);
}

TEST(DebugRecorder, DetachAndStateDestructionAreSafe) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    EventSequence events;
    events.add(0.25f, [] {});
    state->setEvents(&events);

    debug::Recorder recorder;
    recorder.attach(root);
    root.evaluate(0.3f);
    recorder.recordFrame();
    ASSERT_EQ(recorder.eventCount(), 1);
    ASSERT_EQ(recorder.frameCount(), 1);

    recorder.detach();
    EXPECT_EQ(root.eventObserver(), nullptr);
    root.evaluate(0.3f);
    recorder.recordFrame();
    EXPECT_EQ(recorder.eventCount(), 1);
    EXPECT_EQ(recorder.frameCount(), 1);

    recorder.attach(root);
    state->setTimeD(0.0);
    root.evaluate(0.3f);
    recorder.recordFrame();
    EXPECT_EQ(recorder.eventCount(), 1);
    EXPECT_EQ(recorder.frameCount(), 1);

    root.destroyGraph();
    recorder.detach();
    EXPECT_EQ(recorder.frameCount(), 1);
}

TEST(DebugRecorder, JsonIsParseableLimitedAndWritable) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clip = makeClip(1.0f);
    HobiState* state = root.play(clip);
    ASSERT_NE(state, nullptr);

    EventSequence events;
    events.add(0.5f, [] {}, "Mid");
    events.endEvent() = HobiEvent(1.0f, [] {}, "End");
    state->setEvents(&events);

    debug::Recorder recorder;
    recorder.attach(root);
    root.evaluate(0.6f);
    recorder.recordFrame();
    root.evaluate(0.5f);
    recorder.recordFrame();

    const std::string text = recorder.toJson();
    EXPECT_EQ(text, recorder.toJson());

    json::Value value;
    std::string error;
    ASSERT_TRUE(json::parse(text, value, error)) << error;
    ASSERT_TRUE(value.isObject());
    EXPECT_EQ(value.find("version")->number, 1.0);

    const json::Value* frames = value.find("frames");
    ASSERT_NE(frames, nullptr);
    ASSERT_TRUE(frames->isArray());
    ASSERT_EQ(frames->array.size(), 2u);
    const json::Value* firstFrame = frames->at(0);
    ASSERT_NE(firstFrame, nullptr);
    EXPECT_TRUE(firstFrame->find("frame")->isNumber());
    EXPECT_TRUE(firstFrame->find("layers")->isArray());
    EXPECT_TRUE(firstFrame->find("states")->isArray());

    const json::Value* recordedEvents = value.find("events");
    ASSERT_NE(recordedEvents, nullptr);
    ASSERT_TRUE(recordedEvents->isArray());
    ASSERT_EQ(recordedEvents->array.size(), 2u);
    const json::Value* end = recordedEvents->at(1);
    ASSERT_NE(end, nullptr);
    EXPECT_EQ(end->find("kind")->text, "End");
    EXPECT_EQ(end->find("eventIndex")->number, -1.0);
    EXPECT_EQ(end->find("eventNameHash")->number,
              static_cast<double>(hashString("End")));

    json::Value limited;
    ASSERT_TRUE(json::parse(recorder.toJson(1), limited, error)) << error;
    EXPECT_EQ(limited.find("frames")->array.size(), 1u);
    EXPECT_EQ(limited.find("events")->array.size(), 1u);

    const std::string path = std::string(ANIM_TESTS_DIR) + "/debug_recorder_tmp.json";
    ASSERT_TRUE(recorder.writeJsonFile(path, error)) << error;
    std::ifstream input(path);
    ASSERT_TRUE(input.good());
    std::stringstream buffer;
    buffer << input.rdbuf();
    EXPECT_EQ(buffer.str(), text);
    input.close();
    std::remove(path.c_str());

    recorder.clear();
    json::Value cleared;
    ASSERT_TRUE(json::parse(recorder.toJson(), cleared, error)) << error;
    EXPECT_TRUE(cleared.find("frames")->array.empty());
    EXPECT_TRUE(cleared.find("events")->array.empty());
}

TEST(DebugRecorder, SteadyStateFrameCapacityIsReused) {
    Skeleton skeleton = makeSkeleton();
    HobiPlayable root;
    root.createOutput(skeleton);

    BoneLocalClip clipA = makeClip(1.0f);
    BoneLocalClip clipB = makeClip(1.0f);
    HobiLayer* layer = root.layers().getLayer(0);
    ASSERT_NE(layer, nullptr);
    ASSERT_NE(layer->play(clipA), nullptr);
    root.evaluate(0.01f);
    ASSERT_NE(layer->play(clipB), nullptr);
    root.evaluate(0.01f);

    debug::Recorder recorder(makeConfig(300, 16));
    recorder.attach(root);
    for (int i = 0; i < 1000; ++i) {
        root.evaluate(0.01f);
        recorder.recordFrame();
    }

    ASSERT_EQ(recorder.frameCount(), 300);
    const debug::FrameRecord* newest = recorder.frameAt(0);
    ASSERT_NE(newest, nullptr);
    ASSERT_EQ(newest->states.size(), 2u);
    const auto stateCapacity = newest->states.capacity();
    const auto layerCapacity = newest->layers.capacity();
    EXPECT_GE(stateCapacity, 2u);

    for (int i = 0; i < 300; ++i) {
        const debug::FrameRecord* frame = recorder.frameAt(-i);
        ASSERT_NE(frame, nullptr);
        EXPECT_EQ(frame->states.size(), 2u);
        EXPECT_EQ(frame->states.capacity(), stateCapacity);
        EXPECT_EQ(frame->layers.capacity(), layerCapacity);
    }
}
