#include "anim/debug/Snapshot.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "anim/debug/NameResolver.h"
#include "anim/graph/PlayableGraph.h"
#include "anim/graph/PlayableNode.h"
#include "anim/hobi/ClipState.h"
#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/MixerStates.h"
#include "anim/hobi/RetargetClipState.h"
#include "anim/integration/JsonWriter.h"

namespace anim::debug {
namespace {

const char* stateTypeName(const HobiState& state) {
    if (dynamic_cast<const ClipState*>(&state) != nullptr) return "ClipState";
    if (dynamic_cast<const RetargetClipState*>(&state) != nullptr) return "RetargetClipState";
    if (dynamic_cast<const LinearMixerState*>(&state) != nullptr) return "LinearMixerState";
    if (dynamic_cast<const DirectionalMixerState*>(&state) != nullptr) return "DirectionalMixerState";
    if (dynamic_cast<const CartesianMixerState*>(&state) != nullptr) return "CartesianMixerState";
    if (dynamic_cast<const ManualMixerState*>(&state) != nullptr) return "ManualMixerState";
    return "HobiState";
}

uint64_t mixHash(uint64_t hash, uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

uint64_t hashFloat(uint64_t hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return mixHash(hash, bits);
}

uint64_t hashCurve(uint64_t hash, const Curve& curve) {
    hash = mixHash(hash, static_cast<uint64_t>(curve.preWrap));
    hash = mixHash(hash, static_cast<uint64_t>(curve.postWrap));
    hash = mixHash(hash, curve.keys.size());
    for (const Keyframe& key : curve.keys) {
        hash = hashFloat(hash, key.time);
        hash = hashFloat(hash, key.value);
        hash = hashFloat(hash, key.inSlope);
        hash = hashFloat(hash, key.outSlope);
        hash = hashFloat(hash, key.inWeight);
        hash = hashFloat(hash, key.outWeight);
        hash = mixHash(hash, key.tangentMode);
        hash = mixHash(hash, key.weightedMode);
        hash = mixHash(hash, key.constant ? 1u : 0u);
    }
    return hash;
}

uint64_t hashCurves(uint64_t hash, const Curve* curves, size_t count) {
    hash = mixHash(hash, count);
    for (size_t i = 0; i < count; ++i) hash = hashCurve(hash, curves[i]);
    return hash;
}

uint64_t clipHash(const Clip* clip) {
    if (clip == nullptr) return 0;

    uint64_t hash = 14695981039346656037ull;
    hash = mixHash(hash, static_cast<uint64_t>(clip->type));
    hash = hashFloat(hash, clip->length);
    hash = mixHash(hash, clip->looping ? 1u : 0u);
    hash = hashFloat(hash, clip->averageSpeed);
    hash = mixHash(hash, clip->markers.size());
    for (const EventMarker& marker : clip->markers) {
        hash = hashFloat(hash, marker.normalizedTime);
        hash = mixHash(hash, marker.name);
    }
    hash = mixHash(hash, clip->properties.size());
    for (const PropertyChannel& property : clip->properties) {
        hash = mixHash(hash, property.path);
        hash = mixHash(hash, property.attribute);
        hash = hashCurve(hash, property.curve);
    }

    if (const auto* bone = dynamic_cast<const BoneLocalClip*>(clip)) {
        hash = mixHash(hash, bone->channels.size());
        for (const BoneChannel& channel : bone->channels) {
            hash = mixHash(hash, static_cast<uint32_t>(channel.boneIndex));
            hash = mixHash(hash, channel.hasPosition ? 1u : 0u);
            hash = mixHash(hash, channel.hasRotation ? 1u : 0u);
            hash = mixHash(hash, channel.hasScale ? 1u : 0u);
            hash = hashCurves(hash, channel.position, 3);
            hash = hashCurves(hash, channel.rotation, 4);
            hash = hashCurves(hash, channel.scale, 3);
        }
    } else if (const auto* humanoid = dynamic_cast<const HumanoidClip*>(clip)) {
        hash = mixHash(hash, humanoid->muscles.size());
        for (const Curve& muscle : humanoid->muscles) hash = hashCurve(hash, muscle);
        hash = hashCurves(hash, humanoid->rootT, 3);
        hash = hashCurves(hash, humanoid->rootQ, 4);
        hash = hashCurves(hash, humanoid->leftFootT, 3);
        hash = hashCurves(hash, humanoid->rightFootT, 3);
        hash = hashCurves(hash, humanoid->leftFootQ, 4);
        hash = hashCurves(hash, humanoid->rightFootQ, 4);
        hash = hashCurves(hash, humanoid->leftHandT, 3);
        hash = hashCurves(hash, humanoid->rightHandT, 3);
        hash = hashCurves(hash, humanoid->leftHandQ, 4);
        hash = hashCurves(hash, humanoid->rightHandQ, 4);
        hash = mixHash(hash, humanoid->hasRootT ? 1u : 0u);
        hash = mixHash(hash, humanoid->hasRootQ ? 1u : 0u);
        hash = mixHash(hash, humanoid->hasFootT ? 1u : 0u);
        hash = mixHash(hash, humanoid->hasFootQ ? 1u : 0u);
        hash = mixHash(hash, humanoid->hasHandT ? 1u : 0u);
        hash = mixHash(hash, humanoid->hasHandQ ? 1u : 0u);
    }

    return hash != 0 ? hash : 1;
}

int findNodeIndex(const std::vector<NodeInfo>& nodes, PlayableHandle handle) {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].handle == handle) return static_cast<int>(i);
    }
    return -1;
}

void appendBool(std::string& out, bool value) {
    out += value ? "true" : "false";
}

void appendIntArray(std::string& out, const std::vector<int>& values) {
    out += '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ", ";
        out += std::to_string(values[i]);
    }
    out += ']';
}

void appendFloatArray(std::string& out, const float* values, size_t count) {
    out += '[';
    for (size_t i = 0; i < count; ++i) {
        if (i != 0) out += ", ";
        out += json::number(values[i]);
    }
    out += ']';
}

void appendFloatArray(std::string& out, const std::vector<float>& values) {
    out += '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ", ";
        out += json::number(values[i]);
    }
    out += ']';
}

std::string vec3Json(const Vec3& value) {
    std::string out;
    appendFloatArray(out, &value.x, 3);
    return out;
}

std::string quatJson(const Quat& value) {
    const Vec4 xyzw = quatToXYZW(value);
    std::string out;
    appendFloatArray(out, &xyzw.x, 4);
    return out;
}

}

void captureInto(const HobiPlayable& playable, Snapshot& out, const NameResolver* names) {
    const PlayableGraph& graph = playable.graph();
    const PlayableGraphDiagnostics& diagnostics = graph.diagnostics();

    out.version = 1;
    out.frameId = playable.frameId();
    out.deltaTime = playable.deltaTime();

    out.graph.compiled = graph.compiled();
    out.graph.playing = graph.isPlaying();
    out.graph.dirty = graph.dirty();
    out.graph.speed = playable.speed();
    out.graph.frameId = graph.frameId();
    out.graph.nodeCount = diagnostics.nodeCount;
    out.graph.reachableNodes = diagnostics.reachableNodes;
    out.graph.maxDepth = diagnostics.maxDepth;
    out.graph.lastError.assign(graph.lastError());

    const LayerList& layers = playable.layers();
    const int layerCount = layers.count();
    int stateCount = 0;
    for (int i = 0; i < layerCount; ++i) {
        const HobiLayer* layer = layers.getLayer(i);
        if (layer != nullptr) stateCount += layer->childCount();
    }

    out.layers.resize(static_cast<size_t>(layerCount));
    out.states.resize(static_cast<size_t>(stateCount));

    int stateIndex = 0;
    for (int i = 0; i < layerCount; ++i) {
        LayerInfo& layerInfo = out.layers[static_cast<size_t>(i)];
        HobiLayer* layer = layers.getLayer(i);
        if (layer == nullptr) {
            layerInfo = LayerInfo{};
            layerInfo.index = i;
            layerInfo.states.clear();
            continue;
        }

        const int childCount = layer->childCount();
        layerInfo.index = layer->index();
        layerInfo.weight = layer->weight();
        layerInfo.targetWeight = layer->targetWeight();
        layerInfo.speed = layer->speed();
        layerInfo.additive = layer->isAdditive();
        layerInfo.commandCount = layer->commandCount();
        layerInfo.states.resize(static_cast<size_t>(childCount));

        const int baseIndex = stateIndex;
        HobiState* currentState = layer->currentState();
        layerInfo.currentState = -1;

        for (int child = 0; child < childCount; ++child) {
            HobiState* state = layer->getChild(child);
            layerInfo.states[static_cast<size_t>(child)] = baseIndex + child;
            if (state == nullptr) {
                out.states[static_cast<size_t>(stateIndex++)] = StateInfo{};
                continue;
            }

            if (state == currentState) layerInfo.currentState = baseIndex + child;

            StateInfo& stateInfo = out.states[static_cast<size_t>(stateIndex++)];
            const Clip* clip = state->clip();

            stateInfo.layer = layer->index();
            stateInfo.indexInLayer = child;
            stateInfo.playable = state->playable();
            stateInfo.type.assign(stateTypeName(*state));
            stateInfo.key = state->key();
            stateInfo.clipName.clear();
            if (names != nullptr && clip != nullptr) {
                stateInfo.clipName.assign(names->clipName(*clip));
            }
            stateInfo.clipHash = clipHash(clip);
            stateInfo.length = state->length();
            stateInfo.looping = state->isLooping();
            stateInfo.playing = state->isPlaying();
            stateInfo.active = state->isActive();
            stateInfo.time = state->timeD();
            stateInfo.normalizedTime = state->normalizedTimeD();
            stateInfo.weight = state->weight();
            stateInfo.targetWeight = state->targetWeight();
            stateInfo.effectiveWeight = state->effectiveWeight();
            stateInfo.speed = state->speed();
            stateInfo.effectiveSpeed = state->effectiveSpeed();
            stateInfo.eventCount = state->events() != nullptr ? state->events()->count() : 0;
        }
    }

    const std::vector<PlayableHandle> handles = graph.nodeHandles();
    out.nodes.resize(handles.size());
    for (size_t i = 0; i < handles.size(); ++i) {
        out.nodes[i].handle = handles[i];
    }
    for (size_t i = 0; i < handles.size(); ++i) {
        NodeInfo& nodeInfo = out.nodes[i];
        const PlayableHandle handle = handles[i];
        PlayableNode* node = graph.get(handle);

        if (node == nullptr) {
            nodeInfo.type.clear();
            nodeInfo.name.clear();
            nodeInfo.inputs.clear();
            nodeInfo.inputWeights.clear();
            nodeInfo.inputCount = 0;
            nodeInfo.weight = 0.0f;
            nodeInfo.speed = 1.0f;
            nodeInfo.time = 0.0;
            nodeInfo.duration = 0.0;
            nodeInfo.playing = false;
            nodeInfo.active = false;
            nodeInfo.hasPose = false;
            continue;
        }

        nodeInfo.type.assign(node->typeName());
        nodeInfo.name.assign(node->name());
        nodeInfo.inputCount = node->inputCount();
        nodeInfo.weight = node->weight();
        nodeInfo.speed = node->speed();
        nodeInfo.time = node->time();
        nodeInfo.duration = node->duration();
        nodeInfo.playing = node->isPlaying();
        nodeInfo.active = node->isActive();
        nodeInfo.hasPose = node->hasPose();

        nodeInfo.inputs.resize(static_cast<size_t>(nodeInfo.inputCount));
        nodeInfo.inputWeights.resize(static_cast<size_t>(nodeInfo.inputCount));
        for (int port = 0; port < nodeInfo.inputCount; ++port) {
            nodeInfo.inputs[static_cast<size_t>(port)] = findNodeIndex(out.nodes, node->input(port));
            nodeInfo.inputWeights[static_cast<size_t>(port)] = node->inputWeight(port);
        }
    }

    const RootMotionDelta& rootMotion = playable.rootMotionDelta();
    out.rootMotion.hasMotion = rootMotion.hasMotion;
    out.rootMotion.deltaPosition = rootMotion.deltaPosition;
    out.rootMotion.deltaRotation = rootMotion.deltaRotation;
}

Snapshot capture(const HobiPlayable& playable, const NameResolver* names) {
    Snapshot snapshot;
    captureInto(playable, snapshot, names);
    return snapshot;
}

std::string toJson(const Snapshot& snapshot) {
    std::string out;
    out.reserve(4096);
    out += "{\n";
    out += "  \"version\": " + std::to_string(snapshot.version) + ",\n";
    out += "  \"frame\": " + std::to_string(snapshot.frameId) + ",\n";
    out += "  \"deltaTime\": " + json::number(snapshot.deltaTime) + ",\n";

    out += "  \"graph\": {\n";
    out += "    \"compiled\": ";
    appendBool(out, snapshot.graph.compiled);
    out += ",\n    \"playing\": ";
    appendBool(out, snapshot.graph.playing);
    out += ",\n    \"dirty\": ";
    appendBool(out, snapshot.graph.dirty);
    out += ",\n    \"speed\": " + json::number(snapshot.graph.speed);
    out += ",\n    \"frameId\": " + std::to_string(snapshot.graph.frameId);
    out += ",\n    \"nodeCount\": " + std::to_string(snapshot.graph.nodeCount);
    out += ",\n    \"reachableNodes\": " + std::to_string(snapshot.graph.reachableNodes);
    out += ",\n    \"maxDepth\": " + std::to_string(snapshot.graph.maxDepth);
    out += ",\n    \"lastError\": \"" + json::escape(snapshot.graph.lastError) + "\"\n";
    out += "  },\n";

    out += "  \"layers\": [";
    if (!snapshot.layers.empty()) out += '\n';
    for (size_t i = 0; i < snapshot.layers.size(); ++i) {
        const LayerInfo& layer = snapshot.layers[i];
        out += "    {\n";
        out += "      \"index\": " + std::to_string(layer.index) + ",\n";
        out += "      \"weight\": " + json::number(layer.weight) + ",\n";
        out += "      \"targetWeight\": " + json::number(layer.targetWeight) + ",\n";
        out += "      \"speed\": " + json::number(layer.speed) + ",\n";
        out += "      \"additive\": ";
        appendBool(out, layer.additive);
        out += ",\n      \"currentState\": " + std::to_string(layer.currentState);
        out += ",\n      \"commandCount\": " + std::to_string(layer.commandCount);
        out += ",\n      \"states\": ";
        appendIntArray(out, layer.states);
        out += "\n    }";
        if (i + 1 < snapshot.layers.size()) out += ',';
        out += '\n';
    }
    out += "  ],\n";

    out += "  \"states\": [";
    if (!snapshot.states.empty()) out += '\n';
    for (size_t i = 0; i < snapshot.states.size(); ++i) {
        const StateInfo& state = snapshot.states[i];
        out += "    {\n";
        out += "      \"layer\": " + std::to_string(state.layer) + ",\n";
        out += "      \"indexInLayer\": " + std::to_string(state.indexInLayer) + ",\n";
        out += "      \"playable\": {\"index\": " + std::to_string(state.playable.index) +
               ", \"version\": " + std::to_string(state.playable.version) + "},\n";
        out += "      \"type\": \"" + json::escape(state.type) + "\",\n";
        out += "      \"clipName\": \"" + json::escape(state.clipName) + "\",\n";
        out += "      \"clipHash\": " + std::to_string(state.clipHash) + ",\n";
        out += "      \"length\": " + json::number(state.length) + ",\n";
        out += "      \"looping\": ";
        appendBool(out, state.looping);
        out += ",\n      \"playing\": ";
        appendBool(out, state.playing);
        out += ",\n      \"active\": ";
        appendBool(out, state.active);
        out += ",\n      \"time\": " + json::number(state.time);
        out += ",\n      \"normalizedTime\": " + json::number(state.normalizedTime);
        out += ",\n      \"weight\": " + json::number(state.weight);
        out += ",\n      \"targetWeight\": " + json::number(state.targetWeight);
        out += ",\n      \"effectiveWeight\": " + json::number(state.effectiveWeight);
        out += ",\n      \"speed\": " + json::number(state.speed);
        out += ",\n      \"effectiveSpeed\": " + json::number(state.effectiveSpeed);
        out += ",\n      \"eventCount\": " + std::to_string(state.eventCount);
        out += "\n    }";
        if (i + 1 < snapshot.states.size()) out += ',';
        out += '\n';
    }
    out += "  ],\n";

    out += "  \"nodes\": [";
    if (!snapshot.nodes.empty()) out += '\n';
    for (size_t i = 0; i < snapshot.nodes.size(); ++i) {
        const NodeInfo& node = snapshot.nodes[i];
        out += "    {\n";
        out += "      \"handle\": {\"index\": " + std::to_string(node.handle.index) +
               ", \"version\": " + std::to_string(node.handle.version) + "},\n";
        out += "      \"type\": \"" + json::escape(node.type) + "\",\n";
        out += "      \"name\": \"" + json::escape(node.name) + "\",\n";
        out += "      \"inputCount\": " + std::to_string(node.inputCount) + ",\n";
        out += "      \"weight\": " + json::number(node.weight) + ",\n";
        out += "      \"speed\": " + json::number(node.speed) + ",\n";
        out += "      \"time\": " + json::number(node.time) + ",\n";
        out += "      \"duration\": " + json::number(node.duration) + ",\n";
        out += "      \"playing\": ";
        appendBool(out, node.playing);
        out += ",\n      \"active\": ";
        appendBool(out, node.active);
        out += ",\n      \"hasPose\": ";
        appendBool(out, node.hasPose);
        out += ",\n      \"inputs\": ";
        appendIntArray(out, node.inputs);
        out += ",\n      \"inputWeights\": ";
        appendFloatArray(out, node.inputWeights);
        out += "\n    }";
        if (i + 1 < snapshot.nodes.size()) out += ',';
        out += '\n';
    }
    out += "  ],\n";

    out += "  \"rootMotion\": {\n";
    out += "    \"hasMotion\": ";
    appendBool(out, snapshot.rootMotion.hasMotion);
    out += ",\n    \"deltaPosition\": " + vec3Json(snapshot.rootMotion.deltaPosition);
    out += ",\n    \"deltaRotation\": " + quatJson(snapshot.rootMotion.deltaRotation) + "\n";
    out += "  }\n";
    out += "}\n";
    return out;
}

bool writeJsonFile(const Snapshot& snapshot, const std::string& path, std::string& error) {
    return json::writeFile(path, toJson(snapshot), error);
}

}
