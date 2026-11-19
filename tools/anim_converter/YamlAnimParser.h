#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "anim/clip/Curve.h"

namespace anim::converter {

struct ParsedEvent {
    float time = 0.0f;
    std::string functionName;
};

struct ParsedCurveChannel {
    std::string attribute;
    std::string path;
    int classID = 0;

    Curve floatCurve;
    Curve position[3];
    Curve rotation[4];
    Curve scale[3];
    Curve euler[3];

    bool hasFloat = false;
    bool hasPosition = false;
    bool hasRotation = false;
    bool hasScale = false;
    bool hasEuler = false;
};

struct ParsedClip {
    std::string name;
    bool legacy = false;
    bool compressed = false;
    float sampleRate = 0.0f;
    float startTime = 0.0f;
    float stopTime = 0.0f;
    float cycleOffset = 0.0f;
    bool loopTime = false;
    int wrapMode = 0;

    std::vector<ParsedCurveChannel> channels;
    std::vector<ParsedEvent> events;

    float length() const noexcept { return stopTime - startTime; }
};

bool parseAnimationClip(std::string_view yaml, ParsedClip& out, std::string& error);
bool loadAnimationClipFile(const std::string& path, ParsedClip& out, std::string& error);

}  // namespace anim::converter
