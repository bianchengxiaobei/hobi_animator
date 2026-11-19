#include "anim/debug/Recorder.h"

#include <cstddef>
#include <string>

#include "anim/core/Hash.h"
#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/LayerList.h"
#include "anim/integration/JsonWriter.h"

namespace anim::debug {
namespace {

int clampCapacity(int value) noexcept {
    return value > 0 ? value : 0;
}

void appendBool(std::string& out, bool value) {
    out += value ? "true" : "false";
}

void appendPlayable(std::string& out, PlayableHandle handle) {
    out += "{\"index\": " + std::to_string(handle.index) +
           ", \"version\": " + std::to_string(handle.version) + "}";
}

const char* eventKindName(EventKind kind) noexcept {
    return kind == EventKind::End ? "End" : "Normal";
}

void appendFrame(std::string& out, const FrameRecord& frame) {
    out += "    {\n";
    out += "      \"frame\": " + std::to_string(frame.frameId) + ",\n";
    out += "      \"deltaTime\": " + json::number(frame.deltaTime) + ",\n";
    out += "      \"graphSpeed\": " + json::number(frame.graphSpeed) + ",\n";
    out += "      \"graphPlaying\": ";
    appendBool(out, frame.graphPlaying);
    out += ",\n      \"layers\": [";
    if (!frame.layers.empty()) out += '\n';
    for (size_t i = 0; i < frame.layers.size(); ++i) {
        const LayerSample& layer = frame.layers[i];
        out += "        {\"index\": " + std::to_string(layer.index) +
               ", \"weight\": " + json::number(layer.weight) +
               ", \"targetWeight\": " + json::number(layer.targetWeight) +
               ", \"currentState\": " + std::to_string(layer.currentState) + "}";
        if (i + 1 < frame.layers.size()) out += ',';
        out += '\n';
    }
    out += "      ],\n      \"states\": [";
    if (!frame.states.empty()) out += '\n';
    for (size_t i = 0; i < frame.states.size(); ++i) {
        const StateSample& state = frame.states[i];
        out += "        {\"layer\": " + std::to_string(state.layer) +
               ", \"indexInLayer\": " + std::to_string(state.indexInLayer) +
               ", \"playable\": ";
        appendPlayable(out, state.playable);
        out += ", \"time\": " + json::number(state.time) +
               ", \"weight\": " + json::number(state.weight) +
               ", \"targetWeight\": " + json::number(state.targetWeight) +
               ", \"speed\": " + json::number(state.speed) + ", \"playing\": ";
        appendBool(out, state.playing);
        out += '}';
        if (i + 1 < frame.states.size()) out += ',';
        out += '\n';
    }
    out += "      ]\n    }";
}

void appendEvent(std::string& out, const EventRecord& event) {
    out += "    {\n";
    out += "      \"frame\": " + std::to_string(event.frameId) + ",\n";
    out += "      \"statePlayable\": ";
    appendPlayable(out, event.statePlayable);
    out += ",\n      \"layer\": " + std::to_string(event.layer) + ",\n";
    out += "      \"stateIndex\": " + std::to_string(event.stateIndex) + ",\n";
    out += "      \"eventIndex\": " + std::to_string(event.eventIndex) + ",\n";
    out += "      \"kind\": \"" + std::string(eventKindName(event.kind)) + "\",\n";
    out += "      \"normalizedTime\": " + json::number(event.normalizedTime) + ",\n";
    out += "      \"playingForwards\": ";
    appendBool(out, event.playingForwards);
    out += ",\n      \"eventNameHash\": " + std::to_string(event.eventNameHash) + "\n";
    out += "    }";
}

}

Recorder::Recorder(RecorderConfig config)
    : _frameCapacity(clampCapacity(config.frameCapacity)),
      _eventCapacity(clampCapacity(config.eventCapacity)) {}

Recorder::~Recorder() {
    detach();
}

void Recorder::attach(HobiPlayable& playable) {
    if (_playable == &playable) {
        _playable->setEventObserver(this);
        return;
    }
    detach();

    _playable = &playable;
    _frames.resize(static_cast<size_t>(_frameCapacity));
    _events.resize(static_cast<size_t>(_eventCapacity));
    clear();

    const LayerList& layers = playable.layers();
    int stateCount = 0;
    const int layerCount = layers.count();
    for (int i = 0; i < layerCount; ++i) {
        const HobiLayer* layer = layers.getLayer(i);
        if (layer != nullptr) stateCount += layer->childCount();
    }
    for (FrameRecord& frame : _frames) {
        frame.layers.reserve(static_cast<size_t>(layerCount));
        frame.states.reserve(static_cast<size_t>(stateCount));
    }

    _playable->setEventObserver(this);
}

void Recorder::detach() {
    if (_playable != nullptr && _playable->eventObserver() == this) {
        _playable->setEventObserver(nullptr);
    }
    _playable = nullptr;
}

void Recorder::recordFrame() {
    if (_playable == nullptr || _frameCapacity == 0) return;

    int slot = 0;
    if (_frameCount < _frameCapacity) {
        slot = (_frameStart + _frameCount) % _frameCapacity;
        ++_frameCount;
    } else {
        slot = _frameStart;
        _frameStart = (_frameStart + 1) % _frameCapacity;
    }

    FrameRecord& frame = _frames[static_cast<size_t>(slot)];
    frame.frameId = _playable->frameId();
    frame.deltaTime = _playable->deltaTime();
    frame.graphSpeed = _playable->speed();
    frame.graphPlaying = _playable->isGraphPlaying();

    const LayerList& layers = _playable->layers();
    const int layerCount = layers.count();
    int stateCount = 0;
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        const HobiLayer* layer = layers.getLayer(layerIndex);
        if (layer != nullptr) stateCount += layer->childCount();
    }

    frame.layers.resize(static_cast<size_t>(layerCount));
    frame.states.resize(static_cast<size_t>(stateCount));

    int stateOffset = 0;
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        HobiLayer* layer = layers.getLayer(layerIndex);
        LayerSample& layerSample = frame.layers[static_cast<size_t>(layerIndex)];
        if (layer == nullptr) {
            layerSample = LayerSample{};
            layerSample.index = layerIndex;
            continue;
        }

        layerSample.index = layer->index();
        layerSample.weight = layer->weight();
        layerSample.targetWeight = layer->targetWeight();

        HobiState* current = layer->currentState();
        layerSample.currentState = -1;
        const int childCount = layer->childCount();
        for (int child = 0; child < childCount; ++child) {
            HobiState* state = layer->getChild(child);
            StateSample& stateSample = frame.states[static_cast<size_t>(stateOffset)];
            if (state == nullptr) {
                stateSample = StateSample{};
            } else {
                if (state == current) layerSample.currentState = stateOffset;
                stateSample.layer = layer->index();
                stateSample.indexInLayer = child;
                stateSample.playable = state->playable();
                stateSample.time = state->timeD();
                stateSample.weight = state->weight();
                stateSample.targetWeight = state->targetWeight();
                stateSample.speed = state->speed();
                stateSample.playing = state->isPlaying();
            }
            ++stateOffset;
        }
    }
}

int Recorder::frameCount() const noexcept {
    return _frameCount;
}

int Recorder::eventCount() const noexcept {
    return _eventCount;
}

const FrameRecord* Recorder::frameAt(int index) const {
    if (index > 0 || index <= -_frameCount) return nullptr;
    const int offset = -index;
    const int slot = (_frameStart + _frameCount - 1 - offset) % _frameCapacity;
    return &_frames[static_cast<size_t>(slot)];
}

const EventRecord* Recorder::eventAt(int index) const {
    if (index > 0 || index <= -_eventCount) return nullptr;
    const int offset = -index;
    const int slot = (_eventStart + _eventCount - 1 - offset) % _eventCapacity;
    return &_events[static_cast<size_t>(slot)];
}

std::string Recorder::toJson(int maxFrames) const {
    int frameLimit = _frameCount;
    int eventLimit = _eventCount;
    if (maxFrames >= 0) {
        if (frameLimit > maxFrames) frameLimit = maxFrames;
        if (eventLimit > maxFrames) eventLimit = maxFrames;
    }

    std::string out;
    out.reserve(4096 + static_cast<size_t>(frameLimit) * 256 +
                static_cast<size_t>(eventLimit) * 160);
    out += "{\n";
    out += "  \"version\": 1,\n";
    out += "  \"frames\": [";
    if (frameLimit > 0) out += '\n';
    for (int i = 0; i < frameLimit; ++i) {
        appendFrame(out, *frameAt(i - frameLimit + 1));
        if (i + 1 < frameLimit) out += ',';
        out += '\n';
    }
    out += "  ],\n";
    out += "  \"events\": [";
    if (eventLimit > 0) out += '\n';
    for (int i = 0; i < eventLimit; ++i) {
        appendEvent(out, *eventAt(i - eventLimit + 1));
        if (i + 1 < eventLimit) out += ',';
        out += '\n';
    }
    out += "  ]\n";
    out += "}\n";
    return out;
}

bool Recorder::writeJsonFile(const std::string& path, std::string& error) const {
    return json::writeFile(path, toJson(), error);
}

void Recorder::clear() noexcept {
    _frameStart = 0;
    _frameCount = 0;
    _eventStart = 0;
    _eventCount = 0;
}

void Recorder::onEventDispatched(const HobiState& state, const HobiEvent& event,
                                 EventKind kind, int eventIndex,
                                 bool playingForwards) {
    if (_playable == nullptr || _eventCapacity == 0) return;

    int slot = 0;
    if (_eventCount < _eventCapacity) {
        slot = (_eventStart + _eventCount) % _eventCapacity;
        ++_eventCount;
    } else {
        slot = _eventStart;
        _eventStart = (_eventStart + 1) % _eventCapacity;
    }

    EventRecord& record = _events[static_cast<size_t>(slot)];
    HobiLayer* layer = state.layer();
    const HobiPlayable* root = state.root();

    record.frameId = root != nullptr ? root->frameId() : 0;
    record.statePlayable = state.playable();
    record.layer = layer != nullptr ? layer->index() : -1;
    record.stateIndex = -1;
    if (layer != nullptr) {
        for (int i = 0; i < layer->childCount(); ++i) {
            if (layer->getChild(i) == &state) {
                record.stateIndex = i;
                break;
            }
        }
    }
    record.eventIndex = eventIndex;
    record.kind = kind;
    record.normalizedTime = event.normalizedTime;
    record.playingForwards = playingForwards;
    record.eventNameHash = hashString(event.name);
}

}
