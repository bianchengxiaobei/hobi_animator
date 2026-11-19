#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "anim/debug/Snapshot.h"
#include "anim/hobi/FadeMode.h"

namespace anim {
class HobiPlayable;
}

namespace anim::debug {
class Recorder;
}

namespace demo {

class DebugOverlay {
public:
    void setRecorder(anim::debug::Recorder* recorder) noexcept { _recorder = recorder; }
    void setEventNames(
        const std::unordered_map<uint64_t, std::string>* names) noexcept {
        _eventNames = names;
    }

    void setVisible(bool visible) noexcept;
    bool visible() const noexcept { return _visible; }
    void toggleVisible() noexcept { setVisible(!_visible); }

    void toggleEvents() noexcept { _eventsVisible = !_eventsVisible; }
    bool eventsVisible() const noexcept { return _eventsVisible; }

    void setSelectedLayer(int layer) noexcept;
    int selectedLayer() const noexcept { return _selectedLayer; }

    void setPreviewStatus(const std::string& clipName, int clipIndex, int clipCount,
                          float fadeDuration, anim::FadeMode fadeMode,
                          float normalizedStartTime, float speed, bool looping);

    void update(const anim::HobiPlayable& playable, float deltaTime);
    void render();

    const anim::debug::Snapshot& snapshot() const noexcept { return _snapshot; }

    static void dumpGraph(const anim::HobiPlayable& playable);

private:
    anim::debug::Snapshot _snapshot;
    anim::debug::Recorder* _recorder = nullptr;
    const std::unordered_map<uint64_t, std::string>* _eventNames = nullptr;
    float _refreshTimer = 0.0f;
    int _selectedLayer = 0;
    std::string _previewClipName;
    int _previewClipIndex = 0;
    int _previewClipCount = 0;
    float _previewFadeDuration = 0.0f;
    anim::FadeMode _previewFadeMode = anim::FadeMode::FixedSpeed;
    float _previewStartTime = 0.0f;
    float _previewSpeed = 1.0f;
    bool _previewLooping = false;
    bool _previewVisible = false;
    bool _visible = false;
    bool _eventsVisible = true;
    bool _needsCapture = true;
    bool _debugTextEnabled = false;
};

}
