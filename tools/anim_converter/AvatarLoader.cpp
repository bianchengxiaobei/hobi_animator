#include "AvatarLoader.h"

#include <fstream>
#include <sstream>

#include "anim/integration/Json.h"

namespace anim::converter {
namespace {

bool readQuaternion(const json::Value& value, Quat& out, std::string& error) {
    if (!value.isArray() || value.array.size() < 4) {
        error = "rotation must be [x, y, z, w]";
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!value.array[static_cast<std::size_t>(i)].isNumber()) {
            error = "rotation components must be numbers";
            return false;
        }
    }
    const Quat raw = quatFromXYZW(
        static_cast<float>(value.array[0].number),
        static_cast<float>(value.array[1].number),
        static_cast<float>(value.array[2].number),
        static_cast<float>(value.array[3].number));
    if (!isFinite(raw) || glm::dot(raw, raw) <= kEpsilon) {
        error = "rotation must be a non-zero finite quaternion";
        return false;
    }
    out = safeNormalize(raw);
    return true;
}

bool readRotationMap(const json::Value* map, const char* label,
                     std::array<Quat, kHumanBoneCount>& rotations,
                     std::array<bool, kHumanBoneCount>& hasRotation,
                     std::string& error) {
    if (map == nullptr) return true;
    if (!map->isObject()) {
        error = std::string(label) + " must be an object";
        return false;
    }
    for (const auto& entry : map->object) {
        const HumanBone bone = humanBoneFromName(entry.first);
        if (bone == HumanBone::Count) {
            error = std::string("unknown human bone in ") + label + ": " + entry.first;
            return false;
        }
        Quat rotation;
        if (!readQuaternion(entry.second, rotation, error)) {
            error = std::string(label) + "." + entry.first + ": " + error;
            return false;
        }
        rotations[static_cast<std::size_t>(bone)] = rotation;
        hasRotation[static_cast<std::size_t>(bone)] = true;
    }
    return true;
}

bool readVector3(const json::Value* value, Vec3& out, const char* label,
                 std::string& error) {
    if (value == nullptr || !value->isArray() || value->array.size() < 3) {
        error = std::string(label) + " must be [x, y, z]";
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!value->array[static_cast<std::size_t>(i)].isNumber()) {
            error = std::string(label) + " components must be numbers";
            return false;
        }
    }
    out = Vec3(static_cast<float>(value->array[0].number),
               static_cast<float>(value->array[1].number),
               static_cast<float>(value->array[2].number));
    return true;
}

}  // namespace

bool loadAvatarJson(const std::string& text, AvatarConfig& out, std::string& error) {
    json::Value root;
    if (!json::parse(text, root, error)) return false;
    if (!root.isObject()) {
        error = "avatar root must be an object";
        return false;
    }

    out = AvatarConfig{};
    for (Quat& rotation : out.preRot) rotation = kIdentityQuat;
    for (Quat& rotation : out.postRot) rotation = kIdentityQuat;
    if (const json::Value* name = root.find("name"); name != nullptr && name->isString()) {
        out.name = name->text;
    }
    if (const json::Value* hips = root.find("hipsHeight");
        hips != nullptr && hips->isNumber()) {
        out.hipsHeight = static_cast<float>(hips->number);
    }
    const auto readTwist = [&](const char* key, float& value) {
        const json::Value* field = root.find(key);
        if (field != nullptr && field->isNumber()) {
            value = static_cast<float>(field->number);
        }
    };
    readTwist("armTwist", out.armTwist);
    readTwist("foreArmTwist", out.foreArmTwist);
    readTwist("upperLegTwist", out.upperLegTwist);
    readTwist("legTwist", out.legTwist);

    const json::Value* bones = root.find("bones");
    if (bones == nullptr || !bones->isObject()) {
        error = "avatar JSON requires a 'bones' object";
        return false;
    }

    for (const auto& entry : bones->object) {
        const HumanBone bone = humanBoneFromName(entry.first);
        if (bone == HumanBone::Count) {
            error = "unknown human bone: " + entry.first;
            return false;
        }
        if (!entry.second.isString()) {
            error = "bone mapping must be a string: " + entry.first;
            return false;
        }
        out.boneNames[static_cast<std::size_t>(bone)] = entry.second.text;
    }

    if (!readRotationMap(root.find("preRot"), "preRot", out.preRot, out.hasPreRot,
                         error)) {
        return false;
    }
    if (!readRotationMap(root.find("postRot"), "postRot", out.postRot,
                         out.hasPostRot, error)) {
        return false;
    }

    if (const json::Value* uvw = root.find("uvw");
        uvw != nullptr && uvw->isObject()) {
        for (const auto& entry : uvw->object) {
            const HumanBone bone = humanBoneFromName(entry.first);
            if (bone == HumanBone::Count) {
                error = "unknown human bone in uvw: " + entry.first;
                return false;
            }
            if (!entry.second.isArray() || entry.second.array.size() < 3) {
                error = "uvw bindings must be an array of three slots: " +
                        entry.first;
                return false;
            }
            for (int slot = 0; slot < 3; ++slot) {
                const json::Value& value =
                    entry.second.array[static_cast<std::size_t>(slot)];
                if (value.isNull()) continue;
                if (!value.isObject()) {
                    error = "uvw slot must be null or an object: " + entry.first;
                    return false;
                }
                const json::Value* muscle = value.find("muscle");
                const json::Value* sign = value.find("sign");
                const json::Value* minValue = value.find("min");
                const json::Value* maxValue = value.find("max");
                if (muscle == nullptr || !muscle->isString()) {
                    error = "uvw slot requires a string muscle: " + entry.first;
                    return false;
                }
                if (findMuscleId(muscle->text) < 0) {
                    error = "unknown muscle in uvw: " + muscle->text;
                    return false;
                }
                if (sign == nullptr || !sign->isNumber() || minValue == nullptr ||
                    !minValue->isNumber() || maxValue == nullptr ||
                    !maxValue->isNumber()) {
                    error = "uvw slot requires numeric sign/min/max: " + entry.first;
                    return false;
                }
                AvatarConfig::UvwBinding binding;
                binding.muscleName = muscle->text;
                binding.sign = static_cast<float>(sign->number);
                binding.minDegrees = static_cast<float>(minValue->number);
                binding.maxDegrees = static_cast<float>(maxValue->number);
                out.uvw[static_cast<std::size_t>(bone)]
                       [static_cast<std::size_t>(slot)] = std::move(binding);
                out.hasUvw[static_cast<std::size_t>(bone)] = true;
            }
        }
    }

    if (const json::Value* axes = root.find("axes");
        axes != nullptr && axes->isObject()) {
        for (const auto& entry : axes->object) {
            const HumanBone bone = humanBoneFromName(entry.first);
            if (bone == HumanBone::Count) {
                error = "unknown human bone in axes: " + entry.first;
                return false;
            }
            if (!entry.second.isObject()) {
                error = "axes entry must be an object: " + entry.first;
                return false;
            }
            HumanoidBoneAxes parsed;
            const json::Value* preQ = entry.second.find("preQ");
            const json::Value* postQ = entry.second.find("postQ");
            if (preQ == nullptr || postQ == nullptr) {
                error = "axes." + entry.first + " requires preQ/postQ";
                return false;
            }
            if (!readQuaternion(*preQ, parsed.preQ, error) ||
                !readQuaternion(*postQ, parsed.postQ, error) ||
                !readVector3(entry.second.find("sign"), parsed.sign, "axes.sign", error) ||
                !readVector3(entry.second.find("limitMin"), parsed.limitMin,
                             "axes.limitMin", error) ||
                !readVector3(entry.second.find("limitMax"), parsed.limitMax,
                             "axes.limitMax", error)) {
                error = "axes." + entry.first + ": " + error;
                return false;
            }
            const json::Value* length = entry.second.find("length");
            const json::Value* type = entry.second.find("type");
            if (length == nullptr || !length->isNumber() || type == nullptr ||
                !type->isNumber()) {
                error = "axes." + entry.first + " requires numeric length/type";
                return false;
            }
            parsed.length = static_cast<float>(length->number);
            parsed.type = static_cast<int32_t>(type->number);
            parsed.valid = true;
            out.axes[static_cast<std::size_t>(bone)] = parsed;
        }
    }

    if (const json::Value* limits = root.find("limitsOverride");
        limits != nullptr && limits->isObject()) {
        for (const auto& entry : limits->object) {
            if (findMuscleId(entry.first) < 0) {
                error = "unknown muscle in limitsOverride: " + entry.first;
                return false;
            }
            if (!entry.second.isArray() || entry.second.array.size() < 2) {
                error = "limit override must be [min, max]: " + entry.first;
                return false;
            }
            AvatarConfig::LimitOverride limit;
            limit.muscleName = entry.first;
            limit.minDegrees = static_cast<float>(entry.second.array[0].number);
            limit.maxDegrees = static_cast<float>(entry.second.array[1].number);
            out.limits.push_back(std::move(limit));
        }
    }

    return true;
}

bool loadAvatarFile(const std::string& path, AvatarConfig& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return loadAvatarJson(stream.str(), out, error);
}

}  // namespace anim::converter
