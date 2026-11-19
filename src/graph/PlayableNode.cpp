#include "anim/graph/PlayableNode.h"

#include <algorithm>
#include <cmath>

#include "anim/core/Assert.h"
#include "anim/graph/PlayableGraph.h"

namespace anim {

const char* toString(PlayableNodeType type) noexcept {
    switch (type) {
        case PlayableNodeType::Script: return "Script";
        case PlayableNodeType::Mixer: return "Mixer";
        case PlayableNodeType::LayerMixer: return "LayerMixer";
        case PlayableNodeType::Clip: return "Clip";
        case PlayableNodeType::RetargetClip: return "RetargetClip";
        case PlayableNodeType::ScriptPose: return "ScriptPose";
    }
    return "Unknown";
}

void PlayableNode::attach(PlayableGraph* graph, PlayableHandle handle, uint32_t index) noexcept {
    _graph = graph;
    _handle = handle;
    _index = index;
}

void PlayableNode::detach() noexcept {
    _graph = nullptr;
    _handle = {};
    _index = 0;
    _inputs.clear();
    _inputWeights.clear();
}

void PlayableNode::prepareFrame(float deltaTime) {
    if (!_playing || !_autoAdvanceTime) return;
    ANIM_CHECK_MSG(std::isfinite(deltaTime), "deltaTime must be finite");

    _time += static_cast<double>(deltaTime) * static_cast<double>(_speed);

    if (_duration > 0.0) {
        if (_looping) {
            double wrapped = std::fmod(_time, _duration);
            if (wrapped < 0.0) wrapped += _duration;
            _time = wrapped;
        } else {
            _time = std::clamp(_time, 0.0, _duration);
        }
    }
}

const Pose& PlayableNode::outputPose() const { return _output; }

PlayableHandle PlayableNode::input(int port) const {
    ANIM_CHECK_MSG(port >= 0 && port < inputCount(), "input port out of range");
    return _inputs[static_cast<std::size_t>(port)];
}

PlayableNode* PlayableNode::inputNode(int port) {
    if (_graph == nullptr) return nullptr;
    return _graph->get(input(port));
}

const PlayableNode* PlayableNode::inputNode(int port) const {
    if (_graph == nullptr) return nullptr;
    return _graph->get(input(port));
}

float PlayableNode::inputWeight(int port) const {
    ANIM_CHECK_MSG(port >= 0 && port < inputCount(), "input port out of range");
    return _inputWeights[static_cast<std::size_t>(port)];
}

void PlayableNode::setWeight(float weight) {
    ANIM_CHECK_MSG(std::isfinite(weight), "weight must be finite");
    ANIM_CHECK_MSG(weight >= 0.0f, "weight must be non-negative");
    _weight = weight;
}

void PlayableNode::setSpeed(float speed) {
    ANIM_CHECK_MSG(std::isfinite(speed), "speed must be finite");
    _speed = speed;
}

void PlayableNode::setInputHandle(int port, PlayableHandle handle) {
    ANIM_CHECK_MSG(port >= 0, "input port must be non-negative");
    if (port >= inputCount()) resizeInputs(port + 1);
    _inputs[static_cast<std::size_t>(port)] = handle;
}

void PlayableNode::resizeInputs(int count) {
    ANIM_CHECK_MSG(count >= 0, "input count must be non-negative");
    const std::size_t size = static_cast<std::size_t>(count);
    _inputs.resize(size);
    _inputWeights.resize(size, 0.0f);
}

}  // namespace anim
