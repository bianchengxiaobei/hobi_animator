#include "DebugOverlay.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "anim/debug/Recorder.h"
#include "anim/hobi/HobiPlayable.h"

namespace demo {
namespace {

constexpr uint16_t kMaxRows = 24;
constexpr float kRefreshInterval = 0.2f;

const char* stateLabel(const anim::debug::StateInfo& state) {
    return state.clipName.empty() ? state.type.c_str() : state.clipName.c_str();
}

}

void DebugOverlay::setVisible(bool visible) noexcept {
    if (_visible == visible) return;
    _visible = visible;
    if (visible) _needsCapture = true;
}

void DebugOverlay::setSelectedLayer(int layer) noexcept {
    _selectedLayer = layer >= 0 ? layer : 0;
}

void DebugOverlay::setPreviewStatus(const std::string& clipName, int clipIndex, int clipCount,
                                    float fadeDuration, anim::FadeMode fadeMode,
                                    float normalizedStartTime, float speed, bool looping) {
    _previewClipName = clipName;
    _previewClipIndex = clipIndex;
    _previewClipCount = clipCount;
    _previewFadeDuration = fadeDuration;
    _previewFadeMode = fadeMode;
    _previewStartTime = normalizedStartTime;
    _previewSpeed = speed;
    _previewLooping = looping;
    _previewVisible = true;
}

void DebugOverlay::update(const anim::HobiPlayable& playable, float deltaTime) {
    if (!_visible) return;

    _refreshTimer += deltaTime;
    if (!_needsCapture && _refreshTimer < kRefreshInterval) return;

    _refreshTimer = 0.0f;
    _needsCapture = false;
    anim::debug::captureInto(playable, _snapshot);
}

void DebugOverlay::render() {
    if (!_visible) {
        if (_debugTextEnabled) {
            bgfx::setDebug(BGFX_DEBUG_NONE);
            _debugTextEnabled = false;
        }
        return;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);
    _debugTextEnabled = true;
    bgfx::dbgTextClear();

    uint16_t row = 1;
    const anim::debug::GraphInfo& graph = _snapshot.graph;
    bgfx::dbgTextPrintf(1, row++, 0x0f,
                        "frame %llu  dt %.4f  speed %.2f  %s  compiled %d",
                        static_cast<unsigned long long>(_snapshot.frameId),
                        _snapshot.deltaTime, graph.speed,
                        graph.playing ? "playing" : "paused", graph.compiled ? 1 : 0);
    bgfx::dbgTextPrintf(1, row++, 0x0f,
                        "graph: nodes %u reachable %u depth %u dirty %d frameId %llu",
                        graph.nodeCount, graph.reachableNodes, graph.maxDepth,
                        graph.dirty ? 1 : 0,
                        static_cast<unsigned long long>(graph.frameId));

    if (_previewVisible && row < kMaxRows) {
        if (std::isnan(_previewStartTime)) {
            bgfx::dbgTextPrintf(
                1, row++, 0x0f, "preview %d/%d %s  fade %.2f %s  start auto  speed %.2f  loop %s",
                _previewClipIndex, _previewClipCount, _previewClipName.c_str(),
                _previewFadeDuration, anim::toString(_previewFadeMode), _previewSpeed,
                _previewLooping ? "yes" : "no");
        } else {
            bgfx::dbgTextPrintf(
                1, row++, 0x0f,
                "preview %d/%d %s  fade %.2f %s  start %.2f  speed %.2f  loop %s",
                _previewClipIndex, _previewClipCount, _previewClipName.c_str(),
                _previewFadeDuration, anim::toString(_previewFadeMode), _previewStartTime,
                _previewSpeed, _previewLooping ? "yes" : "no");
        }
        if (row < kMaxRows) {
            bgfx::dbgTextPrintf(1, row++, 0x0f,
                                "preview keys  F fade  M mode  T start  -/= speed  L loop  K reset  V skeleton");
        }
    }

    const anim::debug::LayerInfo* layer = nullptr;
    for (const anim::debug::LayerInfo& candidate : _snapshot.layers) {
        if (candidate.index == _selectedLayer) {
            layer = &candidate;
            break;
        }
    }
    if (layer == nullptr && !_snapshot.layers.empty()) layer = &_snapshot.layers.front();

    if (layer != nullptr) {
        bgfx::dbgTextPrintf(
            1, row++, 0x0f, "layer %d  w %.2f (%.2f)  add %d  states %zu  current %d",
            layer->index, layer->weight, layer->targetWeight, layer->additive ? 1 : 0,
            layer->states.size(), layer->currentState);

        int shown = 0;
        for (int stateIndex : layer->states) {
            if (stateIndex < 0 ||
                static_cast<size_t>(stateIndex) >= _snapshot.states.size()) {
                continue;
            }
            if (shown >= 5 || row >= kMaxRows) break;
            const anim::debug::StateInfo& state = _snapshot.states[stateIndex];
            bgfx::dbgTextPrintf(
                3, row++, 0x0f, "[%d] %s  t %.2f/%.2f  n %.2f  w %.2f  s %.2f  ev %d",
                stateIndex, stateLabel(state), state.time, state.length,
                state.normalizedTime, state.effectiveWeight, state.speed,
                state.eventCount);
            ++shown;
        }
        if (static_cast<int>(layer->states.size()) > shown && row < kMaxRows) {
            bgfx::dbgTextPrintf(3, row++, 0x0f, "...");
        }
    } else if (row < kMaxRows) {
        bgfx::dbgTextPrintf(1, row++, 0x0f, "layer %d  <missing>", _selectedLayer);
    }

    if (_eventsVisible && _recorder != nullptr && row < kMaxRows) {
        bgfx::dbgTextPrintf(1, row++, 0x0f, "events:");
        const int count = _recorder->eventCount();
        const int maxOffset = std::min(3, count - 1);
        for (int offset = maxOffset; offset >= 0 && row < kMaxRows; --offset) {
            const anim::debug::EventRecord* event = _recorder->eventAt(-offset);
            if (event == nullptr) continue;
            const std::string* eventName = nullptr;
            if (_eventNames != nullptr) {
                const auto found = _eventNames->find(event->eventNameHash);
                if (found != _eventNames->end()) eventName = &found->second;
            }
            if (eventName != nullptr) {
                bgfx::dbgTextPrintf(
                    3, row++, 0x0f, "f%llu  l%d s%d i%d  %s  %s  n%.2f  %s",
                    static_cast<unsigned long long>(event->frameId), event->layer,
                    event->stateIndex, event->eventIndex,
                    event->kind == anim::EventKind::End ? "End" : "Normal",
                    event->playingForwards ? "fwd" : "back", event->normalizedTime,
                    eventName->c_str());
            } else {
                bgfx::dbgTextPrintf(
                    3, row++, 0x0f, "f%llu  l%d s%d i%d  %s  %s  n%.2f  hash %llx",
                    static_cast<unsigned long long>(event->frameId), event->layer,
                    event->stateIndex, event->eventIndex,
                    event->kind == anim::EventKind::End ? "End" : "Normal",
                    event->playingForwards ? "fwd" : "back", event->normalizedTime,
                    static_cast<unsigned long long>(event->eventNameHash));
            }
        }
    }
}

void DebugOverlay::dumpGraph(const anim::HobiPlayable& playable) {
    const anim::debug::Snapshot snapshot = anim::debug::capture(playable);
    std::cout << "graph nodes=" << snapshot.graph.nodeCount
              << " reachable=" << snapshot.graph.reachableNodes
              << " depth=" << snapshot.graph.maxDepth
              << " frame=" << snapshot.frameId << "\n";
    for (size_t i = 0; i < snapshot.nodes.size(); ++i) {
        const anim::debug::NodeInfo& node = snapshot.nodes[i];
        std::cout << "  node[" << i << "] handle=" << node.handle.index << ":"
                  << node.handle.version << " type=" << node.type
                  << " name=" << node.name << " inputs=";
        for (size_t input = 0; input < node.inputs.size(); ++input) {
            if (input != 0) std::cout << ',';
            std::cout << node.inputs[input];
        }
        std::cout << "\n";
    }
    for (const anim::debug::LayerInfo& layer : snapshot.layers) {
        std::cout << "  layer " << layer.index << " states=";
        for (size_t i = 0; i < layer.states.size(); ++i) {
            if (i != 0) std::cout << ',';
            std::cout << layer.states[i];
        }
        std::cout << " current=" << layer.currentState << "\n";
    }
}

}
