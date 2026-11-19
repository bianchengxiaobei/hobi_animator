#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "anim/graph/PlayableHandle.h"
#include "anim/hobi/EventObserver.h"

namespace anim {
class HobiPlayable;
}

namespace anim::debug {

struct RecorderConfig {
    int frameCapacity = 300;
    int eventCapacity = 256;
};

struct StateSample {
    int layer = -1;
    int indexInLayer = -1;
    PlayableHandle playable{};
    double time = 0.0;
    float weight = 0.0f;
    float targetWeight = 0.0f;
    float speed = 1.0f;
    bool playing = false;
};

struct LayerSample {
    int index = 0;
    float weight = 0.0f;
    float targetWeight = 0.0f;
    int currentState = -1;
};

struct FrameRecord {
    uint64_t frameId = 0;
    float deltaTime = 0.0f;
    float graphSpeed = 1.0f;
    bool graphPlaying = false;
    std::vector<StateSample> states;
    std::vector<LayerSample> layers;
};

struct EventRecord {
    uint64_t frameId = 0;
    PlayableHandle statePlayable{};
    int layer = -1;
    int stateIndex = -1;
    int eventIndex = -1;
    EventKind kind = EventKind::Normal;
    float normalizedTime = 0.0f;
    bool playingForwards = true;
    uint64_t eventNameHash = 0;
};

class Recorder final : public IEventObserver {
public:
    explicit Recorder(RecorderConfig config = {});
    ~Recorder() override;

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void attach(HobiPlayable& playable);
    void detach();

    void recordFrame();

    int frameCount() const noexcept;
    int eventCount() const noexcept;
    const FrameRecord* frameAt(int index) const;
    const EventRecord* eventAt(int index) const;

    std::string toJson(int maxFrames = -1) const;
    bool writeJsonFile(const std::string& path, std::string& error) const;

    void clear() noexcept;

private:
    void onEventDispatched(const HobiState& state, const HobiEvent& event,
                           EventKind kind, int eventIndex,
                           bool playingForwards) override;

    HobiPlayable* _playable = nullptr;
    std::vector<FrameRecord> _frames;
    std::vector<EventRecord> _events;
    int _frameCapacity = 0;
    int _eventCapacity = 0;
    int _frameStart = 0;
    int _frameCount = 0;
    int _eventStart = 0;
    int _eventCount = 0;
};

}
