#include "Validator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "anim/avatar/MuscleTable.h"
#include "anim/clip/ClipIO.h"

namespace anim::validate {
namespace {

std::string indexPath(std::string_view prefix, std::size_t index) {
    return std::string(prefix) + "[" + std::to_string(index) + "]";
}

void addIssue(Report& report, const ClipValidationOptions& options, Severity severity,
              const char* code, std::string path, std::string message) {
    report.add(severity, code, options.file, options.clipName, std::move(path),
               std::move(message));
}

bool isFinite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool isFinite(const Mat4& value) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) return false;
        }
    }
    return true;
}

bool allCurvesEmpty(const Curve* curves, int count) {
    for (int i = 0; i < count; ++i) {
        if (!curves[i].empty()) return false;
    }
    return true;
}

void validateCurve(const Curve& curve, const std::string& path,
                   const ClipValidationOptions& options, Report& report) {
    bool hasPreviousTime = false;
    float previousTime = 0.0f;

    for (std::size_t i = 0; i < curve.keys.size(); ++i) {
        const Keyframe& key = curve.keys[i];
        const std::string keyPath = path + ".keys[" + std::to_string(i) + "]";

        if (!std::isfinite(key.time)) {
            addIssue(report, options, Severity::Error, "curve.key_time_non_finite",
                     keyPath + ".time", "keyframe time must be finite");
        } else {
            if (hasPreviousTime) {
                if (key.time < previousTime) {
                    addIssue(report, options, Severity::Error, "curve.key_time_order",
                             keyPath + ".time", "keyframe times must be non-decreasing");
                } else if (key.time == previousTime) {
                    addIssue(report, options, Severity::Warning, "curve.key_time_duplicate",
                             keyPath + ".time", "duplicate keyframe time");
                }
            }
            previousTime = key.time;
            hasPreviousTime = true;
        }

        if (!std::isfinite(key.value)) {
            addIssue(report, options, Severity::Error, "curve.key_value_non_finite",
                     keyPath + ".value", "keyframe value must be finite");
        }
        if (!std::isfinite(key.inSlope) || !std::isfinite(key.outSlope)) {
            addIssue(report, options, Severity::Error, "curve.key_tangent_non_finite",
                     keyPath, "keyframe tangents must be finite");
        }
        if (!std::isfinite(key.inWeight) || !std::isfinite(key.outWeight)) {
            addIssue(report, options, Severity::Error, "curve.key_weight_non_finite",
                     keyPath, "keyframe weights must be finite");
        }
    }
}

void validateTransformCurves(const Curve* curves, int count, const std::string& path,
                             const ClipValidationOptions& options, Report& report,
                             bool checkZeroQuaternion) {
    if (allCurvesEmpty(curves, count)) {
        addIssue(report, options, Severity::Warning, "curve.empty", path,
                 "transform has no keyframes");
        return;
    }

    for (int i = 0; i < count; ++i) {
        validateCurve(curves[i], path + "[" + std::to_string(i) + "]", options, report);
    }

    if (!checkZeroQuaternion) return;

    bool hasKey = false;
    bool allZero = true;
    for (int i = 0; i < count; ++i) {
        for (const Keyframe& key : curves[i].keys) {
            hasKey = true;
            if (key.value != 0.0f) allZero = false;
        }
    }
    if (hasKey && allZero) {
        addIssue(report, options, Severity::Error, "curve.rotation_zero", path,
                 "all quaternion components are zero");
    }
}

void validateMarkers(const std::vector<EventMarker>& markers,
                     const ClipValidationOptions& options, Report& report) {
    std::unordered_set<StringId> names;
    std::unordered_set<float> times;

    for (std::size_t i = 0; i < markers.size(); ++i) {
        const EventMarker& marker = markers[i];
        const std::string path = indexPath("markers", i);
        const bool validTime =
            std::isfinite(marker.normalizedTime) && marker.normalizedTime >= 0.0f &&
            marker.normalizedTime <= 1.0f;

        if (!validTime) {
            addIssue(report, options, Severity::Error, "event.time_range",
                     path + ".normalizedTime", "event time must be in [0, 1]");
        }
        if (marker.name == kInvalidStringId) {
            addIssue(report, options, Severity::Error, "event.name_invalid",
                     path + ".name", "event name must not be empty");
        }

        bool duplicate = false;
        if (marker.name != kInvalidStringId && !names.insert(marker.name).second) {
            duplicate = true;
        }
        if (validTime && !times.insert(marker.normalizedTime).second) {
            duplicate = true;
        }
        if (duplicate) {
            addIssue(report, options, Severity::Warning, "event.duplicate", path,
                     "event name or time is duplicated");
        }
    }
}

void validateBoneLocal(const BoneLocalClip& clip, const ClipValidationOptions& options,
                       Report& report) {
    if (clip.channels.empty()) {
        addIssue(report, options, Severity::Warning, "clip.no_channels", "channels",
                 "clip has no bone channels");
        return;
    }

    if (options.skeleton == nullptr) {
        addIssue(report, options, Severity::Info, "clip.skeleton_missing",
                 "channels", "bone index range check skipped without a skeleton");
    }

    std::unordered_map<int32_t, std::size_t> firstChannelByBone;
    for (std::size_t i = 0; i < clip.channels.size(); ++i) {
        const BoneChannel& channel = clip.channels[i];
        const std::string path = indexPath("channels", i);

        if (channel.boneIndex < 0) {
            addIssue(report, options, Severity::Error, "clip.bone_index_negative",
                     path + ".boneIndex",
                     "boneIndex " + std::to_string(channel.boneIndex) + " is negative");
        } else if (options.skeleton != nullptr &&
                   channel.boneIndex >= options.skeleton->boneCount()) {
            addIssue(report, options, Severity::Error, "clip.bone_index_range",
                     path + ".boneIndex",
                     "boneIndex " + std::to_string(channel.boneIndex) +
                         " is out of range for skeleton with " +
                         std::to_string(options.skeleton->boneCount()) + " bones");
        }

        const auto [it, inserted] = firstChannelByBone.emplace(channel.boneIndex, i);
        (void)it;
        if (!inserted) {
            addIssue(report, options, Severity::Error, "clip.channel_duplicate",
                     path + ".boneIndex",
                     "boneIndex " + std::to_string(channel.boneIndex) +
                         " is already used by channels[" + std::to_string(it->second) +
                         "]");
        }

        if (!channel.hasPosition && !channel.hasRotation && !channel.hasScale) {
            addIssue(report, options, Severity::Warning, "clip.channel_empty", path,
                     "channel has no position, rotation, or scale curves");
        }
        if (channel.hasPosition) {
            validateTransformCurves(channel.position, 3, path + ".position", options, report,
                                    false);
        }
        if (channel.hasRotation) {
            validateTransformCurves(channel.rotation, 4, path + ".rotation", options, report,
                                    true);
        }
        if (channel.hasScale) {
            validateTransformCurves(channel.scale, 3, path + ".scale", options, report, false);
        }
    }
}

void validateHumanoidCurves(bool hasFlag, const Curve* curves, int count,
                            const std::string& path, bool checkZeroQuaternion,
                            const ClipValidationOptions& options, Report& report) {
    const bool empty = allCurvesEmpty(curves, count);
    if (hasFlag && empty) {
        addIssue(report, options, Severity::Error, "clip.humanoid_curve_missing", path,
                 "humanoid flag is set but the curve data is empty");
        return;
    }
    if (!hasFlag && !empty) {
        addIssue(report, options, Severity::Warning, "clip.humanoid_curve_unflagged", path,
                 "humanoid curve data is present but its flag is not set");
    }
    if (!empty) {
        validateTransformCurves(curves, count, path, options, report, checkZeroQuaternion);
    }
}

void validateHumanoid(const HumanoidClip& clip, const ClipValidationOptions& options,
                      Report& report) {
    if (clip.muscles.size() > static_cast<std::size_t>(kMuscleCount)) {
        addIssue(report, options, Severity::Error, "clip.humanoid_muscle_count", "muscles",
                 "humanoid clip has " + std::to_string(clip.muscles.size()) +
                     " muscles, expected at most " + std::to_string(kMuscleCount));
    }

    bool anyMuscle = false;
    const std::size_t muscleCount = std::min(clip.muscles.size(),
                                             static_cast<std::size_t>(kMuscleCount));
    for (std::size_t i = 0; i < muscleCount; ++i) {
        if (clip.muscles[i].empty()) continue;
        anyMuscle = true;
        validateCurve(clip.muscles[i], "muscles[" + std::to_string(i) + "]", options,
                      report);
    }
    if (!anyMuscle) {
        addIssue(report, options, Severity::Warning, "clip.humanoid_muscles_empty",
                 "muscles", "humanoid clip has no muscle keyframes");
    }

    validateHumanoidCurves(clip.hasRootT, clip.rootT, 3, "rootT", false, options, report);
    validateHumanoidCurves(clip.hasRootQ, clip.rootQ, 4, "rootQ", true, options, report);
    validateHumanoidCurves(clip.hasFootT, clip.leftFootT, 3, "leftFootT", false, options,
                           report);
    validateHumanoidCurves(clip.hasFootT, clip.rightFootT, 3, "rightFootT", false, options,
                           report);
    validateHumanoidCurves(clip.hasFootQ, clip.leftFootQ, 4, "leftFootQ", true, options,
                           report);
    validateHumanoidCurves(clip.hasFootQ, clip.rightFootQ, 4, "rightFootQ", true, options,
                           report);
    validateHumanoidCurves(clip.hasHandT, clip.leftHandT, 3, "leftHandT", false, options,
                           report);
    validateHumanoidCurves(clip.hasHandT, clip.rightHandT, 3, "rightHandT", false, options,
                           report);
    validateHumanoidCurves(clip.hasHandQ, clip.leftHandQ, 4, "leftHandQ", true, options,
                           report);
    validateHumanoidCurves(clip.hasHandQ, clip.rightHandQ, 4, "rightHandQ", true, options,
                           report);
}

std::string classifyDecodeError(const std::string& error) {
    if (error.find("bad .ab magic") != std::string::npos ||
        error.find("invalid .ab magic") != std::string::npos) {
        return "ab.magic";
    }
    if (error.find("unsupported .ab version") != std::string::npos) {
        return "ab.version";
    }
    if (error.find("unsupported clip type") != std::string::npos) {
        return "ab.type";
    }
    if (error.find("unexpected end of stream") != std::string::npos ||
        error.find("unexpected end of file") != std::string::npos ||
        error.find("truncated") != std::string::npos) {
        return "ab.truncated";
    }
    return "ab.decode";
}

std::string boneLabel(const std::vector<std::string>* boneNames, std::size_t index) {
    if (boneNames != nullptr && index < boneNames->size()) {
        return "'" + (*boneNames)[index] + "'";
    }
    return std::to_string(index);
}

void addManifestIssue(Report& report, const ManifestValidationOptions& options,
                      Severity severity, const char* code, const std::string& clip,
                      std::string path, std::string message) {
    report.add(severity, code, options.file, clip, std::move(path), std::move(message));
}

bool manifestHasBone(const ManifestValidationOptions& options, const std::string& name) {
    if (name.empty()) return true;
    if (options.boneNames != nullptr && !options.boneNames->empty()) {
        for (const std::string& bone : *options.boneNames) {
            if (bone == name) return true;
        }
        return false;
    }
    if (options.skeleton != nullptr) {
        return options.skeleton->find(hashString(name)) >= 0;
    }
    return true;
}

}  // namespace

void validateClip(const Clip& clip, const ClipValidationOptions& options, Report& report) {
    if (!std::isfinite(clip.length) || clip.length < 0.0f) {
        addIssue(report, options, Severity::Error, "clip.length_invalid", "length",
                 "clip length must be finite and non-negative");
    } else if (clip.length == 0.0f) {
        addIssue(report, options, Severity::Warning, "clip.length_zero", "length",
                 "clip length is zero; this clip contains no playable duration");
    }
    if (!std::isfinite(clip.averageSpeed)) {
        addIssue(report, options, Severity::Error, "clip.average_speed_invalid",
                 "averageSpeed", "average speed must be finite");
    }

    if (clip.type == ClipType::BoneLocal) {
        validateBoneLocal(static_cast<const BoneLocalClip&>(clip), options, report);
    } else if (clip.type == ClipType::HumanoidMuscle) {
        validateHumanoid(static_cast<const HumanoidClip&>(clip), options, report);
    } else {
        addIssue(report, options, Severity::Error, "clip.type_unknown", "type",
                 "unknown clip type");
    }

    validateMarkers(clip.markers, options, report);
}

std::unique_ptr<Clip> validateClipBytes(ByteSpan data, const ClipValidationOptions& options,
                                        Report& report) {
    std::string error;
    std::unique_ptr<Clip> clip = decodeClip(data, &error);
    if (!clip) {
        report.add(Severity::Error, classifyDecodeError(error), options.file, options.clipName,
                   {}, error.empty() ? "failed to decode .ab" : error);
        return nullptr;
    }
    validateClip(*clip, options, report);
    return clip;
}

void validateSkeleton(const Skeleton& skeleton, const std::string& file,
                      const std::vector<std::string>* boneNames, Report& report) {
    if (skeleton.empty()) {
        report.add(Severity::Error, "skeleton.empty", file, {}, "bones",
                   "skeleton contains no bones");
        return;
    }

    std::unordered_set<StringId> names;
    int rootCount = 0;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& bone = skeleton.bones[i];
        const std::string path = indexPath("bones", i);
        const std::string label = boneLabel(boneNames, i);

        if (bone.name == kInvalidStringId) {
            report.add(Severity::Error, "skeleton.name_invalid", file, {},
                       path + ".name", "bone " + label + " has an invalid name");
        } else if (!names.insert(bone.name).second) {
            report.add(Severity::Error, "skeleton.name_duplicate", file, {},
                       path + ".name", "bone " + label + " has a duplicate name");
        }

        if (bone.parent < -1 || bone.parent >= skeleton.boneCount()) {
            report.add(Severity::Error, "skeleton.parent_range", file, {},
                       path + ".parent", "bone " + label + " has an invalid parent index");
        } else if (bone.parent >= static_cast<int32_t>(i)) {
            report.add(Severity::Error, "skeleton.parent_order", file, {},
                       path + ".parent",
                       "bone " + label + " parent must precede the child");
        } else if (bone.parent < 0) {
            ++rootCount;
        }

        if (!isFinite(bone.restLocal.translation) || !anim::isFinite(bone.restLocal.rotation) ||
            !isFinite(bone.restLocal.scale)) {
            report.add(Severity::Error, "skeleton.rest_non_finite", file, {},
                       path + ".restLocal", "bone " + label + " rest transform is not finite");
        }
        if (!isFinite(bone.inverseBind)) {
            report.add(Severity::Error, "skeleton.inverse_bind_non_finite", file, {},
                       path + ".inverseBind",
                       "bone " + label + " inverse bind matrix is not finite");
        }
    }

    if (rootCount == 0) {
        report.add(Severity::Error, "skeleton.root_missing", file, {}, "bones",
                   "skeleton has no root bone");
    } else if (rootCount > 1) {
        report.add(Severity::Warning, "skeleton.root_multiple", file, {}, "bones",
                   "skeleton has multiple root bones");
    }
}

void validateManifest(const AnimationSet& manifest, const ManifestValidationOptions& options,
                      Report& report) {
    if (manifest.skeletonFile.empty()) {
        addManifestIssue(report, options, Severity::Error, "manifest.skeleton_missing", {},
                         "skeleton", "manifest must reference a skeleton file");
    }
    if (manifest.clips.empty()) {
        addManifestIssue(report, options, Severity::Warning, "manifest.no_clips", {},
                         "clips", "manifest contains no clips");
    }

    std::unordered_set<std::string> names;
    std::unordered_set<std::string> actions;
    for (std::size_t i = 0; i < manifest.clips.size(); ++i) {
        const ClipAssetInfo& clip = manifest.clips[i];
        const std::string prefix = "clips[" + std::to_string(i) + "]";
        const std::string path = indexPath("clips", i);

        if (clip.name.empty()) {
            addManifestIssue(report, options, Severity::Error, "manifest.name_missing", {},
                             path + ".name", "clip name must not be empty");
        } else if (!names.insert(clip.name).second) {
            addManifestIssue(report, options, Severity::Error, "manifest.clip_duplicate",
                             clip.name, path + ".name",
                             "clip name '" + clip.name + "' is duplicated");
        }

        if (clip.action.empty()) {
            addManifestIssue(report, options, Severity::Error, "manifest.action_missing",
                             clip.name, path + ".action",
                             "clip action must not be empty");
        } else if (!actions.insert(clip.action).second) {
            addManifestIssue(report, options, Severity::Error, "manifest.action_duplicate",
                             clip.name, path + ".action",
                             "action '" + clip.action + "' is duplicated");
        }

        if (clip.file.empty()) {
            addManifestIssue(report, options, Severity::Error, "manifest.file_missing",
                             clip.name, path + ".file", "clip file must not be empty");
        }

        if (!std::isfinite(clip.fadeDuration) || clip.fadeDuration < 0.0f) {
            addManifestIssue(report, options, Severity::Error, "manifest.fade_invalid",
                             clip.name, path + ".fade",
                             "fade duration must be finite and non-negative");
        }
        if (!std::isfinite(clip.speed) || clip.speed == 0.0f) {
            addManifestIssue(report, options, Severity::Error, "manifest.speed_invalid",
                             clip.name, path + ".speed",
                             "speed must be finite and non-zero");
        }
        if (!std::isnan(clip.normalizedStartTime) &&
            (!std::isfinite(clip.normalizedStartTime) || clip.normalizedStartTime < 0.0f ||
             clip.normalizedStartTime > 1.0f)) {
            addManifestIssue(report, options, Severity::Error, "manifest.start_time_invalid",
                             clip.name, path + ".normalizedStartTime",
                             "normalized start time must be in [0, 1]");
        }
        if (!clip.rootBone.empty() && !manifestHasBone(options, clip.rootBone)) {
            addManifestIssue(report, options, Severity::Error, "manifest.root_bone_missing",
                             clip.name, path + ".rootBone",
                             "root bone '" + clip.rootBone + "' is not present in the skeleton");
        }

        std::unordered_set<std::string> eventNames;
        for (std::size_t j = 0; j < clip.events.size(); ++j) {
            const ClipEventInfo& event = clip.events[j];
            const std::string eventPath =
                prefix + ".events[" + std::to_string(j) + "]";
            if (event.name.empty()) {
                addManifestIssue(report, options, Severity::Error,
                                 "manifest.event_name_invalid", clip.name,
                                 eventPath + ".name", "event name must not be empty");
            } else if (!eventNames.insert(event.name).second) {
                addManifestIssue(report, options, Severity::Warning,
                                 "manifest.event_duplicate", clip.name,
                                 eventPath + ".name",
                                 "event name '" + event.name + "' is duplicated");
            }
            if (!std::isfinite(event.normalizedTime) || event.normalizedTime < 0.0f ||
                event.normalizedTime > 1.0f) {
                addManifestIssue(report, options, Severity::Error,
                                 "manifest.event_time_range", clip.name,
                                 eventPath + ".normalizedTime",
                                 "event time must be in [0, 1]");
            }
        }
    }
}

void validateClipManifest(const Clip& clip, const ClipAssetInfo& info,
                          std::size_t clipIndex,
                          const ManifestValidationOptions& options, Report& report) {
    std::unordered_set<StringId> markerNames;
    for (const EventMarker& marker : clip.markers) markerNames.insert(marker.name);

    for (std::size_t i = 0; i < info.events.size(); ++i) {
        const ClipEventInfo& event = info.events[i];
        if (event.name.empty()) continue;
        if (markerNames.find(hashString(event.name)) == markerNames.end()) {
            addManifestIssue(report, options, Severity::Warning, "manifest.event_missing",
                             info.name,
                             "clips[" + std::to_string(clipIndex) + "].events[" +
                                 std::to_string(i) + "].name",
                             "event '" + event.name + "' is not present in " + info.file);
        }
    }
}

}  // namespace anim::validate
