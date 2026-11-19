#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace anim::pack {

struct EventSidecarEvent {
    double frame = 0.0;
    std::string name;
};

struct EventSidecarAction {
    std::string action;
    double frameStart = 0.0;
    double frameEnd = 0.0;
    std::vector<EventSidecarEvent> events;
};

struct EventSidecar {
    int version = 1;
    double fps = 0.0;
    std::vector<EventSidecarAction> actions;

    const EventSidecarAction* find(std::string_view actionName) const noexcept {
        for (const EventSidecarAction& action : actions) {
            if (action.action == actionName) return &action;
        }
        return nullptr;
    }
};

bool loadEventSidecar(std::string_view text, EventSidecar& out, std::string& error);
bool loadEventSidecarFile(const std::string& path, EventSidecar& out, std::string& error);

}  // namespace anim::pack
