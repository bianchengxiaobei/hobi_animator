#include "AnimConverter.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <unordered_map>

#include "anim/core/Hash.h"
#include "anim/core/Math.h"

namespace anim::converter {
namespace {

int componentOf(std::string_view attribute) {
    if (attribute.size() < 2 || attribute[attribute.size() - 2] != '.') return -1;
    switch (attribute.back()) {
        case 'x': return 0;
        case 'y': return 1;
        case 'z': return 2;
        case 'w': return 3;
        default: return -1;
    }
}

std::string baseOf(std::string_view attribute) {
    if (componentOf(attribute) >= 0) return std::string(attribute.substr(0, attribute.size() - 2));
    return std::string(attribute);
}

bool isHumanRoot(std::string_view base) {
    return base == "RootT" || base == "RootQ" || base == "LeftFootT" || base == "RightFootT" ||
           base == "LeftFootQ" || base == "RightFootQ" || base == "LeftHandT" ||
           base == "RightHandT" || base == "LeftHandQ" || base == "RightHandQ";
}

bool isHumanAttribute(std::string_view base) {
    return isHumanRoot(base) || findMuscleId(base) >= 0;
}

bool isBoneTransformAttribute(std::string_view base) {
    return base == "m_LocalPosition" || base == "m_LocalRotation" ||
           base == "m_LocalScale";
}

void addPropertyChannel(Clip& clip, const ParsedCurveChannel& source) {
    PropertyChannel property;
    property.path = source.path.empty() ? kInvalidStringId : hashString(source.path);
    property.attribute = hashString(source.attribute);
    property.curve = source.floatCurve;
    clip.properties.push_back(std::move(property));
}

bool assignHumanCurve(HumanoidClip& clip, std::string_view base, int component,
                      const Curve& curve) {
    if (component < 0) return false;

    if (base == "RootT") {
        clip.rootT[component] = curve;
        clip.hasRootT = true;
        return true;
    }
    if (base == "RootQ") {
        clip.rootQ[component] = curve;
        clip.hasRootQ = true;
        return true;
    }
    if (base == "LeftFootT") {
        clip.leftFootT[component] = curve;
        clip.hasFootT = true;
        return true;
    }
    if (base == "RightFootT") {
        clip.rightFootT[component] = curve;
        clip.hasFootT = true;
        return true;
    }
    if (base == "LeftFootQ") {
        clip.leftFootQ[component] = curve;
        clip.hasFootQ = true;
        return true;
    }
    if (base == "RightFootQ") {
        clip.rightFootQ[component] = curve;
        clip.hasFootQ = true;
        return true;
    }
    if (base == "LeftHandT") {
        clip.leftHandT[component] = curve;
        clip.hasHandT = true;
        return true;
    }
    if (base == "RightHandT") {
        clip.rightHandT[component] = curve;
        clip.hasHandT = true;
        return true;
    }
    if (base == "LeftHandQ") {
        clip.leftHandQ[component] = curve;
        clip.hasHandQ = true;
        return true;
    }
    if (base == "RightHandQ") {
        clip.rightHandQ[component] = curve;
        clip.hasHandQ = true;
        return true;
    }
    return false;
}

float curveMaxTime(const ParsedClip& parsed) {
    float maxTime = 0.0f;
    const auto scan = [&](const Curve& curve) {
        if (!curve.keys.empty()) maxTime = std::max(maxTime, curve.keys.back().time);
    };
    for (const ParsedCurveChannel& channel : parsed.channels) {
        scan(channel.floatCurve);
        for (const Curve& curve : channel.position) scan(curve);
        for (const Curve& curve : channel.rotation) scan(curve);
        for (const Curve& curve : channel.scale) scan(curve);
        for (const Curve& curve : channel.euler) scan(curve);
    }
    for (const ParsedEvent& event : parsed.events) maxTime = std::max(maxTime, event.time);
    return maxTime;
}

SkeletonAsset buildSkeletonFromPaths(const ParsedClip& parsed) {
    SkeletonAsset asset;
    std::unordered_map<std::string, int> indices;

    const auto ensureBone = [&](const std::string& key, const std::string& name,
                                const std::string& parentKey) -> int {
        const auto existing = indices.find(key);
        if (existing != indices.end()) return existing->second;

        int parent = -1;
        if (!parentKey.empty()) {
            const auto it = indices.find(parentKey);
            if (it != indices.end()) parent = it->second;
        }

        const int index = asset.skeleton.addBone(hashString(name), parent, Transform{});
        asset.boneNames.push_back(name);
        indices.emplace(key, index);
        return index;
    };

    ensureBone("Root", "Root", "");

    for (const ParsedCurveChannel& channel : parsed.channels) {
        if (channel.path.empty()) continue;
        if (channel.hasFloat && !isBoneTransformAttribute(baseOf(channel.attribute))) continue;

        std::string prefix;
        std::size_t begin = 0;
        while (begin <= channel.path.size()) {
            const std::size_t slash = channel.path.find('/', begin);
            const std::string segment =
                channel.path.substr(begin, slash == std::string::npos ? std::string::npos
                                                                      : slash - begin);
            if (!segment.empty()) {
                const std::string full = prefix.empty() ? segment : prefix + "/" + segment;
                ensureBone(full, segment, prefix.empty() ? "Root" : prefix);
                prefix = full;
            }
            if (slash == std::string::npos) break;
            begin = slash + 1;
        }
    }

    return asset;
}

void addMarkers(const ParsedClip& parsed, Clip& clip) {
    const float length = clip.length;
    for (const ParsedEvent& event : parsed.events) {
        EventMarker marker;
        marker.normalizedTime =
            length > 0.0f ? (event.time - parsed.startTime) / length : 0.0f;
        marker.name = hashString(event.functionName);
        clip.markers.push_back(marker);
    }
}

void convertEulerChannel(const ParsedCurveChannel& source, int boneIndex, BoneLocalClip& clip) {
    std::vector<float> times;
    for (const Curve& curve : source.euler) {
        for (const Keyframe& key : curve.keys) times.push_back(key.time);
    }
    if (times.empty()) return;

    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    BoneChannel channel;
    channel.boneIndex = boneIndex;
    channel.hasRotation = 1;

    std::vector<Quat> rotations;
    rotations.reserve(times.size());
    Quat previous = kIdentityQuat;
    for (float time : times) {
        const Vec3 euler(radians(source.euler[0].evaluateClamped(time)),
                         radians(source.euler[1].evaluateClamped(time)),
                         radians(source.euler[2].evaluateClamped(time)));
        Quat rotation = glm::angleAxis(euler.y, Vec3(0.0f, 1.0f, 0.0f)) *
                        glm::angleAxis(euler.x, Vec3(1.0f, 0.0f, 0.0f)) *
                        glm::angleAxis(euler.z, Vec3(0.0f, 0.0f, 1.0f));
        rotation = safeNormalize(rotation);
        if (glm::dot(previous, rotation) < 0.0f) rotation = -rotation;
        previous = rotation;
        rotations.push_back(rotation);
    }

    for (std::size_t i = 0; i < times.size(); ++i) {
        const std::size_t prev = i > 0 ? i - 1 : i;
        const std::size_t next = i + 1 < times.size() ? i + 1 : i;
        const float dt = times[next] - times[prev];
        const Vec4 values = quatToXYZW(rotations[i]);
        const Vec4 delta = quatToXYZW(rotations[next]) - quatToXYZW(rotations[prev]);
        const Vec4 slope = dt > kEpsilon ? delta / dt : Vec4(0.0f);

        for (int component = 0; component < 4; ++component) {
            Keyframe key;
            key.time = times[i];
            key.value = values[component];
            key.inSlope = slope[component];
            key.outSlope = slope[component];
            channel.rotation[component].keys.push_back(key);
        }
    }

    for (Curve& curve : channel.rotation) {
        curve.preWrap = source.euler[0].preWrap;
        curve.postWrap = source.euler[0].postWrap;
    }
    clip.channels.push_back(std::move(channel));
}

}  // namespace

ConvertResult convertParsed(const ParsedClip& parsed, const ConvertOptions& options) {
    ConvertResult result;

    if (parsed.legacy) {
        result.error = "legacy AnimationClip is not supported";
        return result;
    }
    if (parsed.compressed) {
        result.error = "compressed AnimationClip is not supported";
        return result;
    }

    float length = parsed.length();
    if (length <= 0.0f) length = curveMaxTime(parsed);

    bool humanoid = false;
    for (const ParsedCurveChannel& channel : parsed.channels) {
        if (channel.hasFloat && isHumanAttribute(baseOf(channel.attribute))) {
            humanoid = true;
            break;
        }
    }

    if (humanoid) {
        auto clip = std::make_unique<HumanoidClip>();
        clip->length = length;
        clip->looping = parsed.loopTime;
        clip->muscles.resize(kMuscleCount);

        for (const ParsedCurveChannel& channel : parsed.channels) {
            if (!channel.hasFloat) {
                result.warnings.push_back("ignored non-float channel in humanoid clip");
                continue;
            }

            const std::string base = baseOf(channel.attribute);
            const int component = componentOf(channel.attribute);
            if (assignHumanCurve(*clip, base, component, channel.floatCurve)) continue;

            const int muscle = findMuscleId(base);
            if (muscle >= 0) {
                clip->muscles[static_cast<std::size_t>(muscle)] = channel.floatCurve;
                continue;
            }

            addPropertyChannel(*clip, channel);
        }

        addMarkers(parsed, *clip);
        result.clip = std::move(clip);
        return result;
    }

    SkeletonAsset generatedSkeleton;
    const SkeletonAsset* asset = options.skeleton;
    if (asset == nullptr) {
        generatedSkeleton = buildSkeletonFromPaths(parsed);
        asset = &generatedSkeleton;
        result.warnings.push_back("no skeleton supplied; generated one from animation paths");
    }

    auto clip = std::make_unique<BoneLocalClip>();
    clip->length = length;
    clip->looping = parsed.loopTime;

    std::unordered_map<int, std::size_t> channelByBone;
    const auto ensureChannel = [&](int bone) -> BoneChannel& {
        const auto it = channelByBone.find(bone);
        if (it != channelByBone.end()) return clip->channels[it->second];
        const std::size_t index = clip->channels.size();
        clip->channels.emplace_back();
        clip->channels.back().boneIndex = bone;
        channelByBone.emplace(bone, index);
        return clip->channels.back();
    };

    for (const ParsedCurveChannel& source : parsed.channels) {
        if (source.hasFloat) {
            const std::string base = baseOf(source.attribute);
            const int component = componentOf(source.attribute);
            if (component < 0 || !isBoneTransformAttribute(base)) {
                addPropertyChannel(*clip, source);
                continue;
            }
        }

        const int bone = asset->resolvePath(source.path);
        if (bone < 0) {
            result.warnings.push_back("unresolved bone path: " + source.path);
            continue;
        }

        if (source.hasFloat) {
            const std::string base = baseOf(source.attribute);
            const int component = componentOf(source.attribute);
            BoneChannel& channel = ensureChannel(bone);
            if (base == "m_LocalPosition") {
                channel.position[component] = source.floatCurve;
                channel.hasPosition = 1;
            } else if (base == "m_LocalRotation") {
                channel.rotation[component] = source.floatCurve;
                channel.hasRotation = 1;
            } else if (base == "m_LocalScale") {
                channel.scale[component] = source.floatCurve;
                channel.hasScale = 1;
            }
            continue;
        }

        if (source.hasPosition || source.hasRotation || source.hasScale) {
            BoneChannel& channel = ensureChannel(bone);
            if (source.hasPosition) {
                for (int i = 0; i < 3; ++i) channel.position[i] = source.position[i];
                channel.hasPosition = 1;
            }
            if (source.hasRotation) {
                for (int i = 0; i < 4; ++i) channel.rotation[i] = source.rotation[i];
                channel.hasRotation = 1;
            }
            if (source.hasScale) {
                for (int i = 0; i < 3; ++i) channel.scale[i] = source.scale[i];
                channel.hasScale = 1;
            }
            continue;
        }

        if (source.hasEuler) {
            convertEulerChannel(source, bone, *clip);
            result.warnings.push_back("converted Euler curves to quaternion: " + source.path);
        }
    }

    if (options.avatar != nullptr) {
        for (const std::string& name : options.avatar->boneNames) {
            if (!name.empty() && asset->findByName(name) < 0) {
                result.warnings.push_back("avatar bone not found in skeleton: " + name);
            }
        }
    }

    addMarkers(parsed, *clip);
    result.clip = std::move(clip);
    return result;
}

ConvertResult convertFile(const std::string& path, const ConvertOptions& options) {
    ParsedClip parsed;
    std::string error;
    if (!loadAnimationClipFile(path, parsed, error)) {
        ConvertResult result;
        result.error = std::move(error);
        return result;
    }
    return convertParsed(parsed, options);
}

}  // namespace anim::converter
