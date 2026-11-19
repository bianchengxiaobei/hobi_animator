#include "EventSidecar.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "anim/integration/Json.h"

namespace anim::pack {
namespace {

bool readNumber(const json::Value& value, double& out) {
    if (!value.isNumber() || !std::isfinite(value.number)) return false;
    out = value.number;
    return true;
}

std::string actionContext(std::string_view action, std::string_view field) {
    return "actions['" + std::string(action) + "']." + std::string(field);
}

std::string eventContext(std::string_view action, std::size_t index, std::string_view field) {
    return "actions['" + std::string(action) + "'].events[" + std::to_string(index) + "]." +
           std::string(field);
}

}  // namespace

bool loadEventSidecar(std::string_view text, EventSidecar& out, std::string& error) {
    out = EventSidecar{};
    json::Value root;
    if (!json::parse(text, root, error)) return false;
    if (!root.isObject()) {
        error = "event sidecar root must be an object";
        return false;
    }

    const json::Value* version = root.find("version");
    if (version == nullptr || !version->isNumber() || version->number != 1.0) {
        error = "event sidecar version must be 1";
        return false;
    }

    const json::Value* fps = root.find("fps");
    double fpsValue = 0.0;
    if (fps == nullptr || !readNumber(*fps, fpsValue) || fpsValue <= 0.0) {
        error = "event sidecar fps must be a positive finite number";
        return false;
    }

    const json::Value* actions = root.find("actions");
    if (actions == nullptr || !actions->isObject()) {
        error = "event sidecar actions must be an object";
        return false;
    }

    EventSidecar parsed;
    parsed.version = 1;
    parsed.fps = fpsValue;
    parsed.actions.reserve(actions->object.size());

    std::unordered_set<std::string> actionNames;
    for (const auto& entry : actions->object) {
        const std::string& actionName = entry.first;
        if (actionName.empty()) {
            error = "event sidecar action names must not be empty";
            return false;
        }
        if (!actionNames.insert(actionName).second) {
            error = "event sidecar contains duplicate action '" + actionName + "'";
            return false;
        }

        const json::Value& actionValue = entry.second;
        if (!actionValue.isObject()) {
            error = "actions['" + actionName + "'] must be an object";
            return false;
        }

        EventSidecarAction action;
        action.action = actionName;
        const json::Value* frameStart = actionValue.find("frameStart");
        const json::Value* frameEnd = actionValue.find("frameEnd");
        if (frameStart == nullptr ||
            !readNumber(*frameStart, action.frameStart)) {
            error = actionContext(actionName, "frameStart") +
                    " must be a finite number";
            return false;
        }
        if (frameEnd == nullptr || !readNumber(*frameEnd, action.frameEnd)) {
            error = actionContext(actionName, "frameEnd") +
                    " must be a finite number";
            return false;
        }
        if (action.frameEnd <= action.frameStart) {
            error = actionContext(actionName, "frameEnd") +
                    " must be greater than frameStart";
            return false;
        }

        const json::Value* events = actionValue.find("events");
        if (events == nullptr || !events->isArray()) {
            error = actionContext(actionName, "events") + " must be an array";
            return false;
        }

        action.events.reserve(events->array.size());
        for (std::size_t i = 0; i < events->array.size(); ++i) {
            const json::Value& eventValue = events->array[i];
            if (!eventValue.isObject()) {
                error = "actions['" + actionName + "'].events[" +
                        std::to_string(i) + "] must be an object";
                return false;
            }

            EventSidecarEvent event;
            const json::Value* frame = eventValue.find("frame");
            if (frame == nullptr || !readNumber(*frame, event.frame)) {
                error = eventContext(actionName, i, "frame") +
                        " must be a finite number";
                return false;
            }
            if (event.frame < action.frameStart - 1e-6 ||
                event.frame > action.frameEnd + 1e-6) {
                error = eventContext(actionName, i, "frame") +
                        " must be within [frameStart, frameEnd]";
                return false;
            }

            const json::Value* name = eventValue.find("name");
            if (name == nullptr || !name->isString() || name->text.empty()) {
                error = eventContext(actionName, i, "name") +
                        " must be a non-empty string";
                return false;
            }
            event.name = name->text;
            action.events.push_back(std::move(event));
        }

        parsed.actions.push_back(std::move(action));
    }

    out = std::move(parsed);
    return true;
}

bool loadEventSidecarFile(const std::string& path, EventSidecar& out,
                          std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return loadEventSidecar(stream.str(), out, error);
}

}  // namespace anim::pack
