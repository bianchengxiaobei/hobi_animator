#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "anim/core/Math.h"
#include "anim/graph/PlayableHandle.h"
#include "anim/hobi/Key.h"

namespace anim {
class HobiPlayable;
}

namespace anim::debug {

class NameResolver;

struct GraphInfo {
    bool compiled = false;
    bool playing = false;
    bool dirty = false;
    float speed = 1.0f;
    uint64_t frameId = 0;
    uint32_t nodeCount = 0;
    uint32_t reachableNodes = 0;
    uint32_t maxDepth = 0;
    std::string lastError;
};

struct NodeInfo {
    PlayableHandle handle{};
    std::string type;
    std::string name;
    int inputCount = 0;
    float weight = 0.0f;
    float speed = 1.0f;
    double time = 0.0;
    double duration = 0.0;
    bool playing = false;
    bool active = false;
    bool hasPose = false;
    std::vector<int> inputs;
    std::vector<float> inputWeights;
};

struct StateInfo {
    int layer = -1;
    int indexInLayer = -1;
    PlayableHandle playable{};
    std::string type;
    Key key{};
    std::string clipName;
    uint64_t clipHash = 0;
    float length = 0.0f;
    bool looping = false;
    bool playing = false;
    bool active = false;
    double time = 0.0;
    double normalizedTime = 0.0;
    float weight = 0.0f;
    float targetWeight = 0.0f;
    float effectiveWeight = 0.0f;
    float speed = 1.0f;
    float effectiveSpeed = 1.0f;
    int eventCount = 0;
};

struct LayerInfo {
    int index = 0;
    float weight = 0.0f;
    float targetWeight = 0.0f;
    float speed = 1.0f;
    bool additive = false;
    int currentState = -1;
    int commandCount = 0;
    std::vector<int> states;
};

struct RootMotionInfo {
    bool hasMotion = false;
    Vec3 deltaPosition{0.0f, 0.0f, 0.0f};
    Quat deltaRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct Snapshot {
    int version = 1;
    uint64_t frameId = 0;
    float deltaTime = 0.0f;
    GraphInfo graph;
    std::vector<LayerInfo> layers;
    std::vector<StateInfo> states;
    std::vector<NodeInfo> nodes;
    RootMotionInfo rootMotion;
};

Snapshot capture(const HobiPlayable& playable, const NameResolver* names = nullptr);
void captureInto(const HobiPlayable& playable, Snapshot& out,
                 const NameResolver* names = nullptr);
std::string toJson(const Snapshot& snapshot);
bool writeJsonFile(const Snapshot& snapshot, const std::string& path, std::string& error);

}
