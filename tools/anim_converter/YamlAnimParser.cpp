#include "YamlAnimParser.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace anim::converter {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

int indentOf(std::string_view line) {
    int indent = 0;
    while (indent < static_cast<int>(line.size()) && line[static_cast<std::size_t>(indent)] == ' ')
        ++indent;
    return indent;
}

std::string_view keyOf(std::string_view content) {
    const std::size_t colon = content.find(':');
    return colon == std::string_view::npos ? content : trim(content.substr(0, colon));
}

std::string_view valueOf(std::string_view content) {
    const std::size_t colon = content.find(':');
    return colon == std::string_view::npos ? std::string_view{}
                                          : trim(content.substr(colon + 1));
}

float toFloat(std::string_view text) {
    const std::string value(text);
    return std::strtof(value.c_str(), nullptr);
}

int toInt(std::string_view text) {
    const std::string value(text);
    return std::atoi(value.c_str());
}

int parseVector(std::string_view text, float out[4]) {
    text = trim(text);
    if (text.empty()) return 0;

    if (text.front() != '{') {
        out[0] = toFloat(text);
        return 1;
    }

    if (text.back() == '}') text.remove_suffix(1);
    text.remove_prefix(1);

    int count = 0;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view part = trim(text.substr(0, comma));

        if (!part.empty()) {
            const char axis = part.front();
            const int index = axis == 'x' ? 0 : axis == 'y' ? 1 : axis == 'z' ? 2 : axis == 'w' ? 3 : -1;
            if (index >= 0) {
                out[index] = toFloat(valueOf(part));
                count = std::max(count, index + 1);
            }
        }

        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return count;
}

}  // namespace

bool parseAnimationClip(std::string_view yaml, ParsedClip& out, std::string& error) {
    enum class Section { None, Float, Position, Rotation, Scale, Euler, Events, Settings, Ignore };

    out = ParsedClip{};

    Section section = Section::None;
    bool started = false;

    ParsedCurveChannel item;
    bool itemActive = false;
    Curve* targets[4]{};
    int targetCount = 0;

    Keyframe pending[4];
    bool havePending = false;

    ParsedEvent event;
    bool eventActive = false;

    WrapMode preWrap = WrapMode::ClampForever;
    WrapMode postWrap = WrapMode::ClampForever;

    const auto flushPending = [&] {
        if (!havePending) return;
        for (int i = 0; i < targetCount && i < 4; ++i) {
            Keyframe key = pending[i];
            key.constant = (key.tangentMode & 0x3) == 0x3;
            targets[i]->keys.push_back(key);
        }
        havePending = false;
    };

    const auto finishItem = [&] {
        if (!itemActive) return;
        flushPending();
        for (int i = 0; i < targetCount; ++i) {
            targets[i]->preWrap = preWrap;
            targets[i]->postWrap = postWrap;
        }
        out.channels.push_back(item);
        item = ParsedCurveChannel{};
        itemActive = false;
        targetCount = 0;
        preWrap = WrapMode::ClampForever;
        postWrap = WrapMode::ClampForever;
    };

    const auto beginItem = [&](Section itemSection) {
        finishItem();
        item = ParsedCurveChannel{};
        itemActive = true;
        preWrap = WrapMode::ClampForever;
        postWrap = WrapMode::ClampForever;

        switch (itemSection) {
            case Section::Float:
                targets[0] = &item.floatCurve;
                targetCount = 1;
                item.hasFloat = true;
                break;
            case Section::Position:
                targets[0] = &item.position[0];
                targets[1] = &item.position[1];
                targets[2] = &item.position[2];
                targetCount = 3;
                item.hasPosition = true;
                break;
            case Section::Rotation:
                targets[0] = &item.rotation[0];
                targets[1] = &item.rotation[1];
                targets[2] = &item.rotation[2];
                targets[3] = &item.rotation[3];
                targetCount = 4;
                item.hasRotation = true;
                break;
            case Section::Scale:
                targets[0] = &item.scale[0];
                targets[1] = &item.scale[1];
                targets[2] = &item.scale[2];
                targetCount = 3;
                item.hasScale = true;
                break;
            case Section::Euler:
                targets[0] = &item.euler[0];
                targets[1] = &item.euler[1];
                targets[2] = &item.euler[2];
                targetCount = 3;
                item.hasEuler = true;
                break;
            default:
                targetCount = 0;
                break;
        }
    };

    const auto finishEvent = [&] {
        if (!eventActive) return;
        out.events.push_back(event);
        event = ParsedEvent{};
        eventActive = false;
    };

    std::istringstream stream{std::string(yaml)};
    std::string lineText;
    while (std::getline(stream, lineText)) {
        const std::string_view line(lineText);
        const std::string_view content = trim(line);
        if (content.empty() || content.front() == '%' || content.front() == '#') continue;
        if (content.rfind("---", 0) == 0) {
            if (started) break;
            continue;
        }

        if (content == "AnimationClip:") {
            started = true;
            continue;
        }
        if (!started) continue;

        const int indent = indentOf(line);
        const bool itemDash = indent == 2 && content.rfind("- ", 0) == 0;

        if (section == Section::Ignore) {
            if (indent == 2 && !itemDash) {
                section = Section::None;
            } else {
                continue;
            }
        }

        if (section == Section::Settings) {
            if (indent >= 4) {
                const std::string_view key = keyOf(content);
                const std::string_view value = valueOf(content);
                if (key == "m_StartTime") out.startTime = toFloat(value);
                else if (key == "m_StopTime") out.stopTime = toFloat(value);
                else if (key == "m_CycleOffset") out.cycleOffset = toFloat(value);
                else if (key == "m_LoopTime") out.loopTime = toInt(value) != 0;
                continue;
            }
        }

        if (section == Section::Events) {
            if (itemDash) {
                finishEvent();
                eventActive = true;
                continue;
            }
            if (eventActive && indent >= 4) {
                const std::string_view key = keyOf(content);
                if (key == "time") event.time = toFloat(valueOf(content));
                else if (key == "functionName") event.functionName = std::string(valueOf(content));
                continue;
            }
        }

        const bool curveSection = section == Section::Float || section == Section::Position ||
                                  section == Section::Rotation || section == Section::Scale ||
                                  section == Section::Euler;

        if (curveSection) {
            if (itemDash) {
                beginItem(section);
                continue;
            }

            if (itemActive && indent >= 6 && content.rfind("- serializedVersion", 0) == 0) {
                flushPending();
                for (auto& key : pending) key = Keyframe{};
                havePending = true;
                continue;
            }

            if (itemActive && indent >= 6 && havePending) {
                const std::string_view key = keyOf(content);
                const std::string_view value = valueOf(content);

                if (key == "m_PreInfinity") {
                    preWrap = static_cast<WrapMode>(toInt(value));
                } else if (key == "m_PostInfinity") {
                    postWrap = static_cast<WrapMode>(toInt(value));
                } else if (key == "time") {
                    pending[0].time = toFloat(value);
                    for (int i = 1; i < targetCount; ++i) pending[i].time = pending[0].time;
                } else if (key == "value" || key == "inSlope" || key == "outSlope" ||
                           key == "inWeight" || key == "outWeight") {
                    float components[4]{0.0f, 0.0f, 0.0f, 0.0f};
                    const int count = parseVector(value, components);
                    for (int i = 0; i < targetCount && i < count; ++i) {
                        if (key == "value") pending[i].value = components[i];
                        else if (key == "inSlope") pending[i].inSlope = components[i];
                        else if (key == "outSlope") pending[i].outSlope = components[i];
                        else if (key == "inWeight") pending[i].inWeight = components[i];
                        else pending[i].outWeight = components[i];
                    }
                } else if (key == "tangentMode") {
                    for (int i = 0; i < targetCount; ++i)
                        pending[i].tangentMode = static_cast<uint8_t>(toInt(value));
                } else if (key == "weightedMode") {
                    for (int i = 0; i < targetCount; ++i)
                        pending[i].weightedMode = static_cast<uint8_t>(toInt(value));
                }
                continue;
            }

            if (itemActive && indent >= 4) {
                const std::string_view key = keyOf(content);
                const std::string_view value = valueOf(content);
                if (key == "attribute") item.attribute = std::string(value);
                else if (key == "path") item.path = std::string(value);
                else if (key == "classID") item.classID = toInt(value);
                continue;
            }
        }

        if (indent == 2 && !itemDash) {
            finishItem();
            finishEvent();

            const std::string_view key = keyOf(content);
            const std::string_view value = valueOf(content);

            if (key == "m_FloatCurves") section = Section::Float;
            else if (key == "m_PositionCurves") section = Section::Position;
            else if (key == "m_RotationCurves") section = Section::Rotation;
            else if (key == "m_ScaleCurves") section = Section::Scale;
            else if (key == "m_EulerCurves") section = Section::Euler;
            else if (key == "m_AnimationClipSettings") section = Section::Settings;
            else if (key == "m_Events") section = Section::Events;
            else if (key == "m_EditorCurves" || key == "m_EulerEditorCurves" ||
                     key == "m_CompressedRotationCurves")
                section = Section::Ignore;
            else
                section = Section::None;

            if (key == "m_Name") out.name = std::string(value);
            else if (key == "m_Legacy") out.legacy = toInt(value) != 0;
            else if (key == "m_Compressed") out.compressed = toInt(value) != 0;
            else if (key == "m_SampleRate") out.sampleRate = toFloat(value);
            else if (key == "m_WrapMode") out.wrapMode = toInt(value);
        }
    }

    finishItem();
    finishEvent();

    if (!started) {
        error = "no AnimationClip document found";
        return false;
    }

    if (out.stopTime < out.startTime) {
        error = "invalid clip time range";
        return false;
    }

    return true;
}

bool loadAnimationClipFile(const std::string& path, ParsedClip& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return parseAnimationClip(stream.str(), out, error);
}

}  // namespace anim::converter
