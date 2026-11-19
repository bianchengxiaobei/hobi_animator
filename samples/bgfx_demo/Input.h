#pragma once

#include <cstdint>

namespace demo {

enum class Key : uint32_t {
    Space,
    Up,
    Down,
    Tab,
    Period,
    LeftBracket,
    RightBracket,
    R,
    D,
    E,
    G,
    S,
    F,
    M,
    T,
    L,
    K,
    V,
    Minus,
    Equals,
};

struct InputState {
    int clipDelta = 0;
    int layerDelta = 0;
    int speedDelta = 0;
    int previewSpeedDelta = 0;
    bool restartRequested = false;
    bool toggleOverlay = false;
    bool toggleEvents = false;
    bool togglePlay = false;
    bool stepRequested = false;
    bool dumpGraph = false;
    bool saveSnapshot = false;
    bool cyclePreviewFade = false;
    bool cyclePreviewFadeMode = false;
    bool cyclePreviewStart = false;
    bool togglePreviewLoop = false;
    bool resetPreview = false;
    bool toggleSkeleton = false;

    void onKeyDown(Key key);
};

}  // namespace demo
