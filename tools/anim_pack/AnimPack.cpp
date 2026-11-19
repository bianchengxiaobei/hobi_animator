#include "AnimPack.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ClipBuilder.h"
#include "EventSidecar.h"
#include "GltfImporter.h"
#include "ManifestWriter.h"
#include "SkeletonWriter.h"
#include "anim/clip/ClipIO.h"
#include "anim/integration/AnimationManifest.h"
#include "anim/integration/Coordinate.h"

namespace anim::pack {
namespace {

namespace fs = std::filesystem;

std::string stripPrefix(const std::string& value, const std::string& prefix) {
    if (!prefix.empty() && value.size() >= prefix.size() &&
        value.compare(0, prefix.size(), prefix) == 0) {
        return value.substr(prefix.size());
    }
    return value;
}

bool isSafeRelativePath(const fs::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    for (const fs::path& part : path) {
        if (part == "..") return false;
    }
    return true;
}

bool writeBytes(const std::string& path, const std::vector<uint8_t>& bytes,
                std::string& error) {
    const fs::path target(path);
    const fs::path parent = target.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            error = "cannot create directory '" + parent.string() + "': " + ec.message();
            return false;
        }
    }

    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "cannot write '" + path + "'";
        return false;
    }
    return true;
}

void mirrorSkeleton(ImportedAsset& asset) {
    for (ImportedBone& bone : asset.bones) {
        bone.restLocal = flipZ(bone.restLocal);
        bone.inverseBind = flipZ(bone.inverseBind);
    }
}

std::string relativeTo(const fs::path& base, const fs::path& path) {
    const fs::path relative = path.lexically_relative(base);
    if (!relative.empty()) return relative.generic_string();
    return path.generic_string();
}

}  // namespace

bool packAnimations(const PackOptions& options, PackResult& result,
                    std::vector<std::string>& warnings, std::string& error) {
    result = PackResult{};
    warnings.clear();
    error.clear();

    if (options.input.empty()) {
        error = "--input is required";
        return false;
    }
    if (options.outDir.empty()) {
        error = "--out-dir is required";
        return false;
    }
    if (options.manifest.empty()) {
        error = "--manifest is required";
        return false;
    }

    std::ifstream manifestFile(options.manifest, std::ios::binary);
    if (!manifestFile) {
        error = "cannot open " + options.manifest;
        return false;
    }
    std::ostringstream manifestText;
    manifestText << manifestFile.rdbuf();

    AnimationSet manifest;
    if (!loadAnimationManifest(manifestText.str(), manifest, error)) return false;

    std::string coordinate = options.coordinate;
    if (coordinate.empty()) coordinate = manifest.coordinate;
    if (coordinate != "gltf" && coordinate != "mirrored") {
        error = "coordinate must be 'gltf' or 'mirrored'";
        return false;
    }
    manifest.coordinate = coordinate;
    const bool mirrored = coordinate == "mirrored";

    EventSidecar eventSidecar;
    const bool hasEventSidecar = !options.events.empty();
    if (hasEventSidecar &&
        !loadEventSidecarFile(options.events, eventSidecar, error)) {
        return false;
    }

    ImportedAsset asset;
    if (!importGltfFile(options.input, asset, warnings, error)) return false;
    result.animationCount = static_cast<int>(asset.animations.size());
    result.boneCount = static_cast<int>(asset.bones.size());

    if (mirrored) mirrorSkeleton(asset);

    std::unordered_map<std::string, std::size_t> animationsByAction;
    for (std::size_t i = 0; i < asset.animations.size(); ++i) {
        const std::string action = stripPrefix(asset.animations[i].name,
                                               options.stripActionPrefix);
        if (action.empty()) {
            error = "animation '" + asset.animations[i].name +
                    "' becomes empty after stripping its action prefix";
            return false;
        }
        if (!animationsByAction.emplace(action, i).second) {
            error = "multiple animations map to action '" + action + "'";
            return false;
        }
    }

    const auto findAnimation = [&](const std::string& action) -> int {
        auto it = animationsByAction.find(action);
        if (it != animationsByAction.end()) return static_cast<int>(it->second);
        const std::string stripped = stripPrefix(action, options.stripActionPrefix);
        it = animationsByAction.find(stripped);
        if (it != animationsByAction.end()) return static_cast<int>(it->second);
        return -1;
    };

    const auto findSidecarAction = [&](const std::string& action) -> int {
        if (!hasEventSidecar) return -1;
        for (std::size_t i = 0; i < eventSidecar.actions.size(); ++i) {
            if (eventSidecar.actions[i].action == action) {
                return static_cast<int>(i);
            }
        }
        const std::string stripped = stripPrefix(action, options.stripActionPrefix);
        if (stripped != action) {
            for (std::size_t i = 0; i < eventSidecar.actions.size(); ++i) {
                if (eventSidecar.actions[i].action == stripped) {
                    return static_cast<int>(i);
                }
            }
        }
        if (!options.stripActionPrefix.empty()) {
            const std::string prefixed = options.stripActionPrefix + action;
            for (std::size_t i = 0; i < eventSidecar.actions.size(); ++i) {
                if (eventSidecar.actions[i].action == prefixed) {
                    return static_cast<int>(i);
                }
            }
        }
        return -1;
    };

    std::vector<bool> sidecarUsed(eventSidecar.actions.size(), false);

    const fs::path outDir = fs::absolute(fs::path(options.outDir)).lexically_normal();
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        error = "cannot create output directory '" + outDir.string() + "': " + ec.message();
        return false;
    }

    const fs::path inputPath(options.input);
    const std::string stem = inputPath.stem().string();
    const fs::path skeletonPath =
        options.skeletonOut.empty()
            ? outDir / (stem + ".skeleton.json")
            : fs::absolute(fs::path(options.skeletonOut)).lexically_normal();
    manifest.skeletonFile = relativeTo(outDir, skeletonPath);

    const fs::path manifestName = fs::path(options.manifest).filename();
    if (manifestName.empty()) {
        error = "cannot determine output manifest name";
        return false;
    }
    const fs::path manifestOut = outDir / manifestName;

    std::unordered_set<std::string> outputPaths;
    outputPaths.insert(skeletonPath.lexically_normal().generic_string());
    outputPaths.insert(manifestOut.lexically_normal().generic_string());

    struct BuiltClip {
        ClipAssetInfo info;
        std::vector<uint8_t> bytes;
        PackClipSummary summary;
    };
    std::vector<BuiltClip> built;
    built.reserve(manifest.clips.size());

    for (ClipAssetInfo sourceInfo : manifest.clips) {
        const int sidecarIndex = findSidecarAction(sourceInfo.action);
        if (sidecarIndex >= 0) {
            const EventSidecarAction& sidecarAction =
                eventSidecar.actions[static_cast<std::size_t>(sidecarIndex)];
            sidecarUsed[static_cast<std::size_t>(sidecarIndex)] = true;
            const double range = sidecarAction.frameEnd - sidecarAction.frameStart;
            sourceInfo.events.clear();
            sourceInfo.events.reserve(sidecarAction.events.size());
            for (const EventSidecarEvent& sidecarEvent : sidecarAction.events) {
                float normalized = static_cast<float>(
                    (sidecarEvent.frame - sidecarAction.frameStart) / range);
                normalized = std::clamp(normalized, 0.0f, 1.0f);
                ClipEventInfo event;
                event.normalizedTime = normalized;
                event.name = sidecarEvent.name;
                sourceInfo.events.push_back(std::move(event));
            }
        }

        const int animationIndex = findAnimation(sourceInfo.action);
        if (animationIndex < 0) {
            error = "clip '" + sourceInfo.name + "' action '" + sourceInfo.action +
                    "' is not present in the glTF";
            return false;
        }

        const fs::path relativeFile(sourceInfo.file);
        if (!isSafeRelativePath(relativeFile)) {
            error = "clip '" + sourceInfo.name +
                    "' file must be a relative path inside --out-dir";
            return false;
        }
        const fs::path outFile = (outDir / relativeFile).lexically_normal();
        const std::string outKey = outFile.generic_string();
        if (!outputPaths.insert(outKey).second) {
            error = "clip '" + sourceInfo.name + "' output file '" +
                    relativeFile.generic_string() + "' is already in use";
            return false;
        }

        BoneLocalClip clip;
        if (!buildBoneLocalClip(asset.animations[static_cast<std::size_t>(animationIndex)],
                                asset, sourceInfo, mirrored, clip, error)) {
            return false;
        }

        std::vector<uint8_t> bytes = encodeClip(clip, &error);
        if (bytes.empty()) {
            if (error.empty()) error = "cannot encode clip '" + sourceInfo.name + "'";
            return false;
        }

        const auto decoded = decodeClip(bytes, &error);
        if (!decoded) {
            if (error.empty()) error = "cannot decode clip '" + sourceInfo.name + "'";
            return false;
        }
        if (decoded->type != clip.type ||
            std::abs(decoded->length - clip.length) > 1e-5f) {
            error = "clip '" + sourceInfo.name + "' failed round-trip verification";
            return false;
        }

        ClipAssetInfo info = sourceInfo;
        info.file = relativeFile.generic_string();
        info.action = sourceInfo.action.empty() ? sourceInfo.name : sourceInfo.action;
        info.name = sourceInfo.name;

        BuiltClip item;
        item.info = std::move(info);
        item.bytes = std::move(bytes);
        item.summary.name = sourceInfo.name;
        item.summary.file = item.info.file;
        item.summary.channelCount = static_cast<int>(clip.channels.size());
        item.summary.length = clip.length;
        item.summary.eventCount = static_cast<int>(clip.markers.size());
        for (const BoneChannel& channel : clip.channels) {
            if (channel.hasPosition != 0) {
                item.summary.keyCount += static_cast<int>(channel.position[0].keys.size());
            }
            if (channel.hasRotation != 0) {
                item.summary.keyCount += static_cast<int>(channel.rotation[0].keys.size());
            }
            if (channel.hasScale != 0) {
                item.summary.keyCount += static_cast<int>(channel.scale[0].keys.size());
            }
        }
        built.push_back(std::move(item));
    }

    for (std::size_t i = 0; i < eventSidecar.actions.size(); ++i) {
        if (!sidecarUsed[i]) {
            error = "event sidecar action '" + eventSidecar.actions[i].action +
                    "' is not used by any manifest clip";
            return false;
        }
    }

    if (!writeSkeletonFile(asset, skeletonPath.string(), error)) return false;

    manifest.clips.clear();
    manifest.clips.reserve(built.size());
    for (BuiltClip& item : built) {
        const fs::path outFile = outDir / fs::path(item.info.file);
        if (!writeBytes(outFile.string(), item.bytes, error)) return false;
        manifest.clips.push_back(item.info);
    }

    if (!writeManifestFile(manifest, manifestOut.string(), error)) return false;

    result.clips.reserve(built.size());
    for (const BuiltClip& item : built) result.clips.push_back(item.summary);
    return true;
}

}  // namespace anim::pack
