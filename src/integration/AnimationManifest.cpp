#include "anim/integration/AnimationManifest.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "anim/integration/Json.h"

namespace anim {
namespace {

bool readFiniteFloat(const json::Value& value, float& out) {
    if (!value.isNumber()) return false;
    const float converted = static_cast<float>(value.number);
    if (!std::isfinite(converted)) return false;
    out = converted;
    return true;
}

std::string clipContext(std::size_t index, std::string_view field) {
    return "clips[" + std::to_string(index) + "]." + std::string(field);
}

bool parseEvent(const json::Value& value, std::size_t clipIndex, std::size_t eventIndex,
                ClipEventInfo& out, std::string& error) {
    const std::string prefix =
        "clips[" + std::to_string(clipIndex) + "].events[" + std::to_string(eventIndex) + "]";
    if (!value.isObject()) {
        error = prefix + " must be an object";
        return false;
    }

    const json::Value* time = value.find("normalizedTime");
    float normalizedTime = 0.0f;
    if (time == nullptr || !readFiniteFloat(*time, normalizedTime)) {
        error = prefix + ".normalizedTime must be a finite number";
        return false;
    }
    if (normalizedTime < 0.0f || normalizedTime > 1.0f) {
        error = prefix + ".normalizedTime must be in [0, 1]";
        return false;
    }

    const json::Value* name = value.find("name");
    if (name == nullptr || !name->isString() || name->text.empty()) {
        error = prefix + ".name must be a non-empty string";
        return false;
    }

    out.normalizedTime = normalizedTime;
    out.name = name->text;
    return true;
}

bool parseClip(const json::Value& value, std::size_t index, ClipAssetInfo& out,
               std::string& error) {
    const std::string prefix = "clips[" + std::to_string(index) + "]";
    if (!value.isObject()) {
        error = prefix + " must be an object";
        return false;
    }

    const json::Value* name = value.find("name");
    if (name == nullptr || !name->isString() || name->text.empty()) {
        error = prefix + ".name must be a non-empty string";
        return false;
    }
    out.name = name->text;

    const json::Value* action = value.find("action");
    if (action == nullptr) {
        out.action = out.name;
    } else if (!action->isString() || action->text.empty()) {
        error = prefix + ".action must be a non-empty string";
        return false;
    } else {
        out.action = action->text;
    }

    const json::Value* file = value.find("file");
    if (file == nullptr || !file->isString() || file->text.empty()) {
        error = prefix + ".file must be a non-empty string";
        return false;
    }
    out.file = file->text;

    if (const json::Value* loop = value.find("loop")) {
        if (!loop->isBool()) {
            error = prefix + ".loop must be a boolean";
            return false;
        }
        out.looping = loop->boolean;
    }

    if (const json::Value* fade = value.find("fade")) {
        if (!readFiniteFloat(*fade, out.fadeDuration) || out.fadeDuration < 0.0f) {
            error = prefix + ".fade must be a finite non-negative number";
            return false;
        }
    }

    if (const json::Value* speed = value.find("speed")) {
        if (!readFiniteFloat(*speed, out.speed) || out.speed == 0.0f) {
            error = prefix + ".speed must be a finite non-zero number";
            return false;
        }
    }

    if (const json::Value* start = value.find("normalizedStartTime")) {
        if (!readFiniteFloat(*start, out.normalizedStartTime) ||
            out.normalizedStartTime < 0.0f || out.normalizedStartTime > 1.0f) {
            error = prefix + ".normalizedStartTime must be in [0, 1]";
            return false;
        }
    }

    if (const json::Value* rootBone = value.find("rootBone")) {
        if (!rootBone->isString()) {
            error = prefix + ".rootBone must be a string";
            return false;
        }
        out.rootBone = rootBone->text;
    }

    if (const json::Value* events = value.find("events")) {
        if (!events->isArray()) {
            error = prefix + ".events must be an array";
            return false;
        }
        out.events.reserve(events->array.size());
        for (std::size_t i = 0; i < events->array.size(); ++i) {
            ClipEventInfo event;
            if (!parseEvent(events->array[i], index, i, event, error)) return false;
            out.events.push_back(std::move(event));
        }
    }

    return true;
}

void resolvePath(std::string& path, const std::filesystem::path& base) {
    if (path.empty()) return;
    const std::filesystem::path candidate(path);
    if (candidate.is_relative()) path = (base / candidate).lexically_normal().string();
}

}  // namespace

bool loadAnimationManifest(std::string_view text, AnimationSet& out, std::string& error) {
    out = AnimationSet{};
    json::Value root;
    if (!json::parse(text, root, error)) return false;
    if (!root.isObject()) {
        error = "animation manifest root must be an object";
        return false;
    }

    AnimationSet parsed;

    const json::Value* version = root.find("version");
    if (version == nullptr || !version->isNumber() || version->number != 1.0) {
        error = "animation manifest version must be 1";
        return false;
    }
    parsed.version = 1;

    const json::Value* skeleton = root.find("skeleton");
    if (skeleton == nullptr || !skeleton->isString() || skeleton->text.empty()) {
        error = "animation manifest skeleton must be a non-empty string";
        return false;
    }
    parsed.skeletonFile = skeleton->text;

    if (const json::Value* coordinate = root.find("coordinate")) {
        if (!coordinate->isString()) {
            error = "animation manifest coordinate must be a string";
            return false;
        }
        if (coordinate->text != "gltf" && coordinate->text != "mirrored") {
            error = "animation manifest coordinate must be 'gltf' or 'mirrored'";
            return false;
        }
        parsed.coordinate = coordinate->text;
    }

    const json::Value* clips = root.find("clips");
    if (clips == nullptr || !clips->isArray()) {
        error = "animation manifest clips must be an array";
        return false;
    }

    parsed.clips.reserve(clips->array.size());
    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < clips->array.size(); ++i) {
        ClipAssetInfo clip;
        if (!parseClip(clips->array[i], i, clip, error)) return false;
        if (!names.insert(clip.name).second) {
            error = clipContext(i, "name") + " duplicates clip name '" + clip.name + "'";
            return false;
        }
        parsed.clips.push_back(std::move(clip));
    }

    out = std::move(parsed);
    return true;
}

bool loadAnimationManifestFile(const std::string& path, AnimationSet& out,
                               std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    if (!loadAnimationManifest(stream.str(), out, error)) return false;

    const std::filesystem::path base = std::filesystem::path(path).parent_path();
    resolvePath(out.skeletonFile, base);
    for (ClipAssetInfo& clip : out.clips) resolvePath(clip.file, base);
    return true;
}

}  // namespace anim
