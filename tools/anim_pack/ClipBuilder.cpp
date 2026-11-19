#include "ClipBuilder.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

#include "anim/integration/Coordinate.h"

namespace anim::pack {
namespace {

float component(const Vec4& value, std::size_t index) {
    switch (index) {
        case 0:
            return value.x;
        case 1:
            return value.y;
        case 2:
            return value.z;
        default:
            return value.w;
    }
}

Vec4 mirrorQuaternionTangent(const Vec4& value) {
    return Vec4(-value.x, -value.y, value.z, value.w);
}

Vec4 mirrorVectorTangent(const Vec4& value) {
    return Vec4(value.x, value.y, -value.z, 0.0f);
}

}  // namespace

bool buildBoneLocalClip(const ImportedAnimation& animation, const ImportedAsset& asset,
                        const ClipAssetInfo& info, bool mirrored, BoneLocalClip& out,
                        std::string& error) {
    out = BoneLocalClip{};
    error.clear();

    if (animation.channels.empty()) {
        error = "animation '" + animation.name + "' contains no channels";
        return false;
    }

    if (!info.rootBone.empty()) {
        bool found = false;
        for (const ImportedBone& bone : asset.bones) {
            if (bone.name == info.rootBone) {
                found = true;
                break;
            }
        }
        if (!found) {
            error = "clip '" + info.name + "' rootBone '" + info.rootBone +
                    "' is not present in the skeleton";
            return false;
        }
    }

    std::vector<BoneChannel> channels;
    std::unordered_map<int, std::size_t> channelByBone;

    for (const ImportedChannel& source : animation.channels) {
        if (source.boneIndex < 0 ||
            source.boneIndex >= static_cast<int>(asset.bones.size())) {
            error = "animation '" + animation.name + "' has an out-of-range bone index";
            return false;
        }

        const std::size_t count = source.keys.size();
        if (count == 0) {
            error = "animation '" + animation.name + "' has an empty channel";
            return false;
        }

        std::vector<Vec4> values(count);
        std::vector<Vec4> inTangents(count, Vec4(0.0f));
        std::vector<Vec4> outTangents(count, Vec4(0.0f));

        for (std::size_t i = 0; i < count; ++i) {
            const ImportedKey& key = source.keys[i];
            if (!std::isfinite(key.time) || key.time < 0.0f) {
                error = "animation '" + animation.name +
                        "' contains a negative or non-finite key time";
                return false;
            }
            if (i > 0 && key.time <= source.keys[i - 1].time) {
                error = "animation '" + animation.name +
                        "' key times must be strictly increasing";
                return false;
            }

            if (source.path == AnimationPath::Rotation) {
                const Quat rotation =
                    quatFromXYZW(key.value[0], key.value[1], key.value[2], key.value[3]);
                if (!isFinite(rotation) || glm::dot(rotation, rotation) <= kEpsilon) {
                    error = "animation '" + animation.name +
                            "' contains an invalid rotation key";
                    return false;
                }
                Quat value = safeNormalize(rotation);
                if (mirrored) value = flipZ(value);
                values[i] = Vec4(value.x, value.y, value.z, value.w);

                if (source.interpolation == Interpolation::CubicSpline) {
                    Vec4 inTangent(key.inTangent[0], key.inTangent[1], key.inTangent[2],
                                   key.inTangent[3]);
                    Vec4 outTangent(key.outTangent[0], key.outTangent[1],
                                    key.outTangent[2], key.outTangent[3]);
                    if (mirrored) {
                        inTangent = mirrorQuaternionTangent(inTangent);
                        outTangent = mirrorQuaternionTangent(outTangent);
                    }
                    inTangents[i] = inTangent;
                    outTangents[i] = outTangent;
                }
            } else {
                Vec3 value(key.value[0], key.value[1], key.value[2]);
                Vec3 inTangent(key.inTangent[0], key.inTangent[1], key.inTangent[2]);
                Vec3 outTangent(key.outTangent[0], key.outTangent[1], key.outTangent[2]);
                if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
                    !std::isfinite(value.z) || !std::isfinite(inTangent.x) ||
                    !std::isfinite(inTangent.y) || !std::isfinite(inTangent.z) ||
                    !std::isfinite(outTangent.x) || !std::isfinite(outTangent.y) ||
                    !std::isfinite(outTangent.z)) {
                    error = "animation '" + animation.name +
                            "' contains a non-finite vector key";
                    return false;
                }
                if (mirrored && source.path == AnimationPath::Translation) {
                    value = flipZ(value);
                    inTangent = flipZ(inTangent);
                    outTangent = flipZ(outTangent);
                }
                values[i] = Vec4(value, 0.0f);
                inTangents[i] = Vec4(inTangent, 0.0f);
                outTangents[i] = Vec4(outTangent, 0.0f);
            }
        }

        if (source.path == AnimationPath::Rotation) {
            for (std::size_t i = 1; i < count; ++i) {
                if (glm::dot(values[i - 1], values[i]) >= 0.0f) continue;
                values[i] = -values[i];
                inTangents[i] = -inTangents[i];
                outTangents[i] = -outTangents[i];
            }
        }

        std::vector<Vec4> inSlopes(count, Vec4(0.0f));
        std::vector<Vec4> outSlopes(count, Vec4(0.0f));

        if (source.interpolation == Interpolation::Linear) {
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t previous = i > 0 ? i - 1 : i;
                const std::size_t next = i + 1 < count ? i + 1 : i;
                const float duration = source.keys[next].time - source.keys[previous].time;
                if (duration > kEpsilon) {
                    const Vec4 slope = (values[next] - values[previous]) / duration;
                    inSlopes[i] = slope;
                    outSlopes[i] = slope;
                }
            }
        } else if (source.interpolation == Interpolation::CubicSpline) {
            inSlopes = inTangents;
            outSlopes = outTangents;
        }

        std::size_t channelIndex = 0;
        const auto existing = channelByBone.find(source.boneIndex);
        if (existing != channelByBone.end()) {
            channelIndex = existing->second;
        } else {
            channelIndex = channels.size();
            channelByBone.emplace(source.boneIndex, channelIndex);
            channels.push_back(BoneChannel{});
            channels.back().boneIndex = source.boneIndex;
        }

        BoneChannel& channel = channels[channelIndex];
        Curve* curves = nullptr;
        std::size_t componentCount = 3;
        if (source.path == AnimationPath::Translation) {
            if (channel.hasPosition != 0) {
                error = "animation '" + animation.name +
                        "' has duplicate translation channels for one bone";
                return false;
            }
            channel.hasPosition = 1;
            curves = channel.position;
        } else if (source.path == AnimationPath::Rotation) {
            if (channel.hasRotation != 0) {
                error = "animation '" + animation.name +
                        "' has duplicate rotation channels for one bone";
                return false;
            }
            channel.hasRotation = 1;
            curves = channel.rotation;
            componentCount = 4;
        } else {
            if (channel.hasScale != 0) {
                error = "animation '" + animation.name +
                        "' has duplicate scale channels for one bone";
                return false;
            }
            channel.hasScale = 1;
            curves = channel.scale;
        }

        for (std::size_t componentIndex = 0; componentIndex < componentCount;
             ++componentIndex) {
            Curve& curve = curves[componentIndex];
            curve.keys.reserve(count);
            curve.preWrap = info.looping ? WrapMode::Loop : WrapMode::ClampForever;
            curve.postWrap = info.looping ? WrapMode::Loop : WrapMode::ClampForever;

            for (std::size_t i = 0; i < count; ++i) {
                Keyframe key;
                key.time = source.keys[i].time;
                key.value = component(values[i], componentIndex);
                key.inSlope = component(inSlopes[i], componentIndex);
                key.outSlope = component(outSlopes[i], componentIndex);
                key.constant = source.interpolation == Interpolation::Step && i + 1 < count;
                curve.keys.push_back(key);
            }
        }
    }

    std::sort(channels.begin(), channels.end(),
              [](const BoneChannel& a, const BoneChannel& b) {
                  return a.boneIndex < b.boneIndex;
              });
    out.channels = std::move(channels);
    out.length = animation.length;
    out.looping = info.looping;

    for (const ClipEventInfo& event : info.events) {
        if (event.normalizedTime < 0.0f || event.normalizedTime > 1.0f ||
            event.name.empty()) {
            error = "clip '" + info.name + "' contains an invalid event";
            return false;
        }
        EventMarker marker;
        marker.normalizedTime = event.normalizedTime;
        marker.name = hashString(event.name);
        out.markers.push_back(marker);
    }

    return true;
}

}  // namespace anim::pack
