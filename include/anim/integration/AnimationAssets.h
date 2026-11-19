#pragma once

#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace anim {

struct ClipEventInfo {
    float normalizedTime = 0.0f;
    std::string name;
};

struct ClipAssetInfo {
    std::string name;
    std::string action;
    std::string file;
    bool looping = false;
    float fadeDuration = 0.25f;
    float speed = 1.0f;
    float normalizedStartTime = std::numeric_limits<float>::quiet_NaN();
    std::string rootBone;
    std::vector<ClipEventInfo> events;
};

struct AnimationSet {
    int version = 1;
    std::string skeletonFile;
    std::string coordinate = "gltf";
    std::vector<ClipAssetInfo> clips;

    const ClipAssetInfo* find(std::string_view name) const noexcept {
        for (const ClipAssetInfo& clip : clips) {
            if (clip.name == name) return &clip;
        }
        return nullptr;
    }

    ClipAssetInfo* find(std::string_view name) noexcept {
        for (ClipAssetInfo& clip : clips) {
            if (clip.name == name) return &clip;
        }
        return nullptr;
    }
};

using EventCallback = std::function<void()>;
using EventCallbackMap = std::unordered_map<std::string, EventCallback>;

}  // namespace anim
