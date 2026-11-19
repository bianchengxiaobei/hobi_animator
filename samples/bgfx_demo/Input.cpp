#include "Input.h"

namespace demo {

void InputState::onKeyDown(Key key) {
    switch (key) {
        case Key::Space: togglePlay = true; break;
        case Key::Up: ++clipDelta; break;
        case Key::Down: --clipDelta; break;
        case Key::Tab: ++layerDelta; break;
        case Key::Period: stepRequested = true; break;
        case Key::LeftBracket: --speedDelta; break;
        case Key::RightBracket: ++speedDelta; break;
        case Key::R: restartRequested = true; break;
        case Key::D: toggleOverlay = true; break;
        case Key::E: toggleEvents = true; break;
        case Key::G: dumpGraph = true; break;
        case Key::S: saveSnapshot = true; break;
        case Key::F: cyclePreviewFade = true; break;
        case Key::M: cyclePreviewFadeMode = true; break;
        case Key::T: cyclePreviewStart = true; break;
        case Key::L: togglePreviewLoop = true; break;
        case Key::K: resetPreview = true; break;
        case Key::V: toggleSkeleton = true; break;
        case Key::Minus: --previewSpeedDelta; break;
        case Key::Equals: ++previewSpeedDelta; break;
    }
}

}  // namespace demo
